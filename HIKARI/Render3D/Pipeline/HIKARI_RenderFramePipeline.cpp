#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"

#include <algorithm>

#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE {

    namespace {
        ScreenSpacePassContext BuildScreenSpaceContext() {
            ScreenSpacePassContext context{};
            context.cmd = SERVICES::gCtx.cmdList;
            context.sceneDsv = POST::PostSystem::GetCurrentRenderTargetDsv();
            context.sceneDepthSrv = SERVICES::gCtx.sceneDepthSrv;
            context.depthReadable = context.sceneDepthSrv.ptr != 0;

            int width = POST::PostSystem::GetSceneColorWidth();
            int height = POST::PostSystem::GetSceneColorHeight();
            if (width <= 0 || height <= 0) {
                POST::PostSystem::GetSceneCaptureSize(width, height);
            }
            context.width = static_cast<uint32_t>(std::max(1, width));
            context.height = static_cast<uint32_t>(std::max(1, height));
            return context;
        }
    }

    bool RenderMeshLightingFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment) {

        if (!MESHRENDERER::HasSubmittedItems()) {
            return true;
        }

        GFX::PIX::ScopedGpuEvent pixFrame(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "RenderFrame.MeshLighting");
        if (!MESHRENDERER::BeginFrame(camera, environment)) {
            MESHRENDERER::EndFrame();
            return false;
        }

        const RENDER3D::RenderQueue& queue = MESHRENDERER::BuildRenderQueue();
        const MESHRENDERER::CameraCB* cameraCb = MESHRENDERER::GetCameraConstants();

        RENDER3D::SCREENSPACE::ScreenSpaceFrameResult screenResult{};
        if (cameraCb != nullptr) {
            screenResult = RENDER3D::SCREENSPACE::ExecuteScreenSpacePreLightingPasses(
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState(),
                BuildScreenSpaceContext(),
                *cameraCb,
                environment,
                queue);
        }
        else {
            RENDER3D::SCREENSPACE::EnsureScreenSpaceFallbacks(RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState());
            screenResult.fallbackAoTextureHandle =
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState().fallbackAoTextureHandle;
        }

        MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(screenResult.ssaoRendered);

        const bool opaqueOk = MESHRENDERER::RenderForwardOpaquePass(
            queue,
            screenResult.aoSrv,
            screenResult.fallbackAoTextureHandle);
        bool depthAwareOk = true;
        if (opaqueOk) {
            depthAwareOk = MESHRENDERER::RenderDepthAwarePass(
                queue,
                screenResult.aoSrv,
                screenResult.fallbackAoTextureHandle);
        }

        MESHRENDERER::EndFrame();
        return opaqueOk && depthAwareOk;
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

        const RENDER3D::RenderQueue& queue = MESHRENDERER::BuildRenderQueue();
        MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(false);

        // Capture は後処理と depth-aware phase を含めない。
        const bool opaqueOk = MESHRENDERER::RenderForwardOpaquePass(queue, {}, -1);
        MESHRENDERER::EndFrame();
        return opaqueOk;
    }

} // namespace HIKARI::RENDER3D::PIPELINE
