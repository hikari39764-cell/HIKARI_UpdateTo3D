#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"

#include <algorithm>

#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE {

    namespace {
        bool RebindPostRenderTarget(void*) {
            return POST::PostSystem::RebindCurrentRenderTarget();
        }

        bool BeginPostDepthRead(void*) {
            return POST::PostSystem::BeginCurrentRenderTargetDepthRead();
        }

        void EndPostDepthRead(void*) {
            POST::PostSystem::EndCurrentRenderTargetDepthRead();
        }

        ScreenSpacePassContext BuildScreenSpaceContext() {
            ScreenSpacePassContext context{};
            context.cmd = SERVICES::gCtx.cmdList;
            context.sceneDsv = POST::PostSystem::GetCurrentRenderTargetDsv();
            context.sceneDepthSrv = SERVICES::gCtx.sceneDepthSrv;
            context.depthReadable = context.sceneDepthSrv.ptr != 0;
            context.renderTargetAccess.rebind = RebindPostRenderTarget;
            context.renderTargetAccess.beginDepthRead = BeginPostDepthRead;
            context.renderTargetAccess.endDepthRead = EndPostDepthRead;

            int width = 0;
            int height = 0;
            POST::PostSystem::GetSceneCaptureSize(width, height);
            if (width <= 0 || height <= 0) {
                width = POST::PostSystem::GetSceneColorWidth();
                height = POST::PostSystem::GetSceneColorHeight();
            }
            context.width = static_cast<uint32_t>(std::max(1, width));
            context.height = static_cast<uint32_t>(std::max(1, height));
            return context;
        }

        MESHRENDERER::MeshPassResources BuildMeshPassResources(
            const RENDER3D::SCREENSPACE::ScreenSpaceFrameResult& screenResult) {
            MESHRENDERER::MeshPassResources resources{};
            resources.sceneDepthSrv = SERVICES::gCtx.sceneDepthSrv;
            resources.ssaoSrv = screenResult.aoSrv;
            resources.fallbackAoTextureHandle = screenResult.fallbackAoTextureHandle;
            return resources;
        }
    }

    bool RenderMeshLightingFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {

        if (!MESHRENDERER::HasSubmittedItems()) {
            return true;
        }

        GFX::PIX::ScopedGpuEvent pixFrame(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "RenderFrame.MeshLighting");
        const ScreenSpacePassContext screenSpaceContext = BuildScreenSpaceContext();
        if (!MESHRENDERER::BeginFrame(
            camera,
            environment,
            screenSpaceContext.width,
            screenSpaceContext.height,
            debugView)) {
            MESHRENDERER::EndFrame();
            return false;
        }

        const MESHRENDERER::CameraCB* cameraCb = MESHRENDERER::GetCameraConstants();

        RENDER3D::SCREENSPACE::ScreenSpaceFrameResult screenResult{};
        if (cameraCb != nullptr) {
            screenResult = RENDER3D::SCREENSPACE::ExecuteScreenSpacePreLightingPasses(
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState(),
                screenSpaceContext,
                *cameraCb,
                environment);
        }
        else {
            RENDER3D::SCREENSPACE::EnsureScreenSpaceFallbacks(RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState());
            screenResult.fallbackAoTextureHandle =
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState().fallbackAoTextureHandle;
        }

        MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(screenResult.ssaoRendered);
        MESHRENDERER::MeshPassResources opaqueResources = BuildMeshPassResources(screenResult);
        MESHRENDERER::MeshPassResources postOpaqueResources = opaqueResources;

        bool opaqueOk = false;
        {
            GFX::PIX::ScopedGpuEvent pixForward(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "ForwardOpaque");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuForward(
                SERVICES::gCtx.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardOpaque);
            opaqueOk = MESHRENDERER::RenderForwardOpaquePass(
                opaqueResources);
        }
        bool depthAwareOk = true;
        if (opaqueOk) {
            if (POST::PostSystem::CaptureSceneColorSnapshot()) {
                postOpaqueResources.sceneColorSrv = POST::PostSystem::GetSceneColorSrv();
                POST::PostSystem::RebindCurrentRenderTarget();
            }
        }
        if (opaqueOk && MESHRENDERER::HasDepthAwarePassWork()) {
            GFX::PIX::ScopedGpuEvent pixDepthAware(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "DepthAware");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuDepthAware(
                SERVICES::gCtx.cmdList,
                GFX::GPU_PROFILE::Pass::DepthAware);
            if (POST::PostSystem::BeginCurrentRenderTargetDepthRead()) {
                depthAwareOk = MESHRENDERER::RenderDepthAwarePass(
                    postOpaqueResources);
                POST::PostSystem::EndCurrentRenderTargetDepthRead();
            } else {
                depthAwareOk = false;
            }
        }

        bool transparentOk = true;
        if (opaqueOk && depthAwareOk) {
            GFX::PIX::ScopedGpuEvent pixTransparent(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "ForwardTransparent");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuTransparent(
                SERVICES::gCtx.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardTransparent);
            transparentOk = MESHRENDERER::RenderForwardTransparentPass(
                postOpaqueResources);
        }

        MESHRENDERER::EndFrame();
        return opaqueOk && depthAwareOk && transparentOk;
    }

    bool RenderMeshCaptureOpaqueFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height) {

        if (!MESHRENDERER::HasSubmittedItems()) {
            return true;
        }

        GFX::PIX::ScopedGpuEvent pixFrame(
            SERVICES::gCtx.cmdList,
            GFX::PIX::kColorRender,
            "RenderFrame.ReflectionProbeCaptureOpaque");

        if (!MESHRENDERER::BeginFrame(camera, environment, width, height)) {
            MESHRENDERER::EndFrame();
            return false;
        }

        MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(false);

        // Capture は後処理と depth-aware phase を含めない。
        const MESHRENDERER::MeshPassResources captureResources{};
        const bool opaqueOk = MESHRENDERER::RenderForwardOpaquePass(captureResources);
        MESHRENDERER::EndFrame();
        return opaqueOk;
    }

} // namespace HIKARI::RENDER3D::PIPELINE
