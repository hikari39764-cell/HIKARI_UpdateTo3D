#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"

#include <chrono>

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

    namespace {
        ScreenSpaceRuntimeState gScreenSpaceState{};

        using CpuClock = std::chrono::steady_clock;

        float ElapsedMs(CpuClock::time_point start, CpuClock::time_point end) {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }
    }

    ScreenSpaceRuntimeState& GetScreenSpaceRuntimeState() {
        return gScreenSpaceState;
    }

    void ReleaseScreenSpaceRuntimeState() {
        gScreenSpaceState.geometryBuffer.Release();
        gScreenSpaceState.ssaoRenderer.Release();
        RENDER3D::ReleaseTextureResource(gScreenSpaceState.fallbackAoTextureResource);
        gScreenSpaceState.fallbackAoTextureResource = {};
        gScreenSpaceState.fallbackAoTextureHandle = -1;
        gScreenSpaceState.geometryValid = false;
        gScreenSpaceState.ssaoValid = false;
    }

    bool EnsureScreenSpaceFallbacks(ScreenSpaceRuntimeState& state) {
        if (RENDER3D::IsTextureResourceValid(state.fallbackAoTextureResource)) {
            state.fallbackAoTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(state.fallbackAoTextureResource);
            return state.fallbackAoTextureHandle >= 0;
        }

        state.fallbackAoTextureHandle = -1;
        state.fallbackAoTextureResource = RENDER3D::CreateSolidColorTextureResource(
            "screen_space/fallback_ao",
            0xffffffffu,
            RENDER3D::TextureResourceColorSpace::Linear);
        state.fallbackAoTextureHandle =
            RENDER3D::GetTextureResourceBackendHandle(state.fallbackAoTextureResource);
        return RENDER3D::IsTextureResourceValid(state.fallbackAoTextureResource) &&
            state.fallbackAoTextureHandle >= 0;
    }

    ScreenSpaceFrameResult ExecuteScreenSpacePreLightingPasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& cameraCb,
        const SceneEnvironment& environment,
        const RENDER3D::RenderQueue& queue) {

        ScreenSpaceFrameResult result{};
        const bool fallbackReady = EnsureScreenSpaceFallbacks(state);
        if (fallbackReady) {
            result.fallbackAoTextureResource = state.fallbackAoTextureResource;
            result.aoSrv = RENDER3D::GetTextureResourceSrvGpuHandle(state.fallbackAoTextureResource);
        }
        result.fallbackAoTextureHandle = state.fallbackAoTextureHandle;
        BeginSsaoDebugFrame(context.width, context.height, environment.ambientOcclusion);

        if (context.cmd == nullptr) {
            state.ssaoValid = false;
            return result;
        }

        // Screen-space pass は mesh draw の前段で必要な texture だけを作る。
        GFX::PIX::ScopedGpuEvent pixScreenSpace(context.cmd, GFX::PIX::kColorPost, "ScreenSpace.PreLighting");

        const SsaoMode ssaoMode = ResolveEffectiveSsaoMode(environment.ambientOcclusion);
        const bool ssaoRequiresGeometryBuffer = SsaoRequiresGeometryBuffer(environment.ambientOcclusion);

        // Off 時は GeometryBuffer も作らない。
        if (ssaoMode == SsaoMode::Off ||
            environment.ambientOcclusion.editorViewportSuppressed) {
            state.ssaoRenderer.RecordSkipped(
                context.width,
                context.height,
                environment.ambientOcclusion);
            state.geometryValid = false;
            state.ssaoValid = false;
            return result;
        }

        if (ssaoRequiresGeometryBuffer) {
            GFX::PIX::ScopedGpuEvent pixGeometry(context.cmd, GFX::PIX::kColorRender, "GeometryBuffer");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuGeometry(
                context.cmd,
                GFX::GPU_PROFILE::Pass::GeometryBuffer);
            const CpuClock::time_point geometryStart = CpuClock::now();
            result.geometryBufferWritten = MESHRENDERER::RenderGeometryBufferPass(queue, state.geometryBuffer);
            RecordSsaoGeometryBufferDebug(
                result.geometryBufferWritten,
                ElapsedMs(geometryStart, CpuClock::now()),
                state.geometryBuffer.GetFormat());
        }
        else {
            RecordSsaoGeometryBufferDebug(false, 0.0f, DXGI_FORMAT_UNKNOWN);
        }

        state.geometryValid = result.geometryBufferWritten && state.geometryBuffer.IsValid();
        if ((ssaoRequiresGeometryBuffer && !state.geometryValid) ||
            !context.depthReadable ||
            context.sceneDepthSrv.ptr == 0) {
            state.ssaoValid = false;
            return result;
        }

        bool ssaoOk = false;
        if (POST::PostSystem::BeginCurrentRenderTargetDepthRead()) {
            if (ssaoRequiresGeometryBuffer) {
                ssaoOk = state.ssaoRenderer.Render(
                    context.cmd,
                    state.geometryBuffer,
                    context.sceneDepthSrv,
                    cameraCb,
                    environment.ambientOcclusion);
            }
            else {
                ssaoOk = state.ssaoRenderer.RenderDepthOnly(
                    context.cmd,
                    context.width,
                    context.height,
                    context.sceneDepthSrv,
                    cameraCb,
                    environment.ambientOcclusion);
            }
            POST::PostSystem::EndCurrentRenderTargetDepthRead();
        }

        state.ssaoValid = ssaoOk;
        result.ssaoRendered = ssaoOk;
        if (ssaoOk && state.ssaoRenderer.GetAoSrv().ptr != 0) {
            result.aoSrv = state.ssaoRenderer.GetAoSrv();
            GFX::PIX::SetGpuMarker(context.cmd, GFX::PIX::kColorPost, "SSAO.Composite");
            RecordSsaoCompositeDebug(0.0f);
        }
        return result;
    }

} // namespace HIKARI::RENDER3D::SCREENSPACE
