#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"

#include "HIKARI_DxTexture.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

    namespace {
        ScreenSpaceRuntimeState gScreenSpaceState{};
    }

    ScreenSpaceRuntimeState& GetScreenSpaceRuntimeState() {
        return gScreenSpaceState;
    }

    void ReleaseScreenSpaceRuntimeState() {
        gScreenSpaceState.geometryBuffer.Release();
        gScreenSpaceState.ssaoRenderer.Release();
        gScreenSpaceState.geometryValid = false;
        gScreenSpaceState.ssaoValid = false;
    }

    bool EnsureScreenSpaceFallbacks(ScreenSpaceRuntimeState& state) {
        if (state.fallbackAoTextureHandle >= 0) {
            return true;
        }

        state.fallbackAoTextureHandle = DXTEX::DxTextureManager::CreateSolidColorTexture(
            "screen_space/fallback_ao",
            0xffffffffu,
            DXTEX::TextureColorSpace::Linear);
        return state.fallbackAoTextureHandle >= 0;
    }

    ScreenSpaceFrameResult ExecuteScreenSpacePreLightingPasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& cameraCb,
        const SceneEnvironment& environment,
        const RENDER3D::RenderQueue& queue) {

        ScreenSpaceFrameResult result{};
        EnsureScreenSpaceFallbacks(state);
        result.fallbackAoTextureHandle = state.fallbackAoTextureHandle;
        result.aoSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(state.fallbackAoTextureHandle);

        if (context.cmd == nullptr) {
            state.ssaoValid = false;
            return result;
        }

        // Screen-space pass は mesh draw の前段で必要な texture だけを作る。
        GFX::PIX::ScopedGpuEvent pixScreenSpace(context.cmd, GFX::PIX::kColorPost, "ScreenSpace.PreLighting");

        if (!environment.ambientOcclusion.enabled ||
            environment.ambientOcclusion.editorViewportSuppressed) {
            state.ssaoRenderer.RecordSkipped(
                context.width,
                context.height,
                environment.ambientOcclusion);
            state.geometryValid = false;
            state.ssaoValid = false;
            return result;
        }

        {
            GFX::PIX::ScopedGpuEvent pixGeometry(context.cmd, GFX::PIX::kColorRender, "ScreenSpace.GeometryBuffer");
            result.geometryBufferWritten = MESHRENDERER::RenderGeometryBufferPass(queue, state.geometryBuffer);
        }

        state.geometryValid = result.geometryBufferWritten && state.geometryBuffer.IsValid();
        if (!state.geometryValid || !context.depthReadable || context.sceneDepthSrv.ptr == 0) {
            state.ssaoValid = false;
            return result;
        }

        bool ssaoOk = false;
        if (POST::PostSystem::BeginCurrentRenderTargetDepthRead()) {
            GFX::PIX::ScopedGpuEvent pixSsao(context.cmd, GFX::PIX::kColorPost, "ScreenSpace.SSAO");
            ssaoOk = state.ssaoRenderer.Render(
                context.cmd,
                state.geometryBuffer,
                context.sceneDepthSrv,
                cameraCb,
                environment.ambientOcclusion);
            POST::PostSystem::EndCurrentRenderTargetDepthRead();
        }

        state.ssaoValid = ssaoOk;
        result.ssaoRendered = ssaoOk;
        if (ssaoOk && state.ssaoRenderer.GetAoSrv().ptr != 0) {
            result.aoSrv = state.ssaoRenderer.GetAoSrv();
        }
        return result;
    }

} // namespace HIKARI::RENDER3D::SCREENSPACE
