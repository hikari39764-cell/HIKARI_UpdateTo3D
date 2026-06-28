#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

    namespace {
        ScreenSpaceRuntimeState gScreenSpaceState{};

        using CpuClock = std::chrono::steady_clock;

        float ElapsedMs(CpuClock::time_point start, CpuClock::time_point end) {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }

        bool NearlyEqualMat4(const MATH::Mat4& lhs, const MATH::Mat4& rhs) {
            constexpr float kEpsilon = 0.0001f;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    if (std::fabs(lhs.m[col][row] - rhs.m[col][row]) > kEpsilon) {
                        return false;
                    }
                }
            }
            return true;
        }

        float MaxAbsDeltaMat4(const MATH::Mat4& lhs, const MATH::Mat4& rhs) {
            float maxDelta = 0.0f;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    maxDelta = (std::max)(
                        maxDelta,
                        std::fabs(lhs.m[col][row] - rhs.m[col][row]));
                }
            }
            return maxDelta;
        }

        bool IsHzbStatsUsable(
            const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& stats,
            uint32_t width,
            uint32_t height) {

            return
                stats.hzbBuilt &&
                stats.hzbFinestSrv.ptr != 0 &&
                stats.width == width &&
                stats.height == height &&
                stats.hzbWidth != 0 &&
                stats.hzbHeight != 0 &&
                stats.hzbViewProjValid;
        }

        bool IsHzbStatsUsableForView(
            const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& stats,
            uint32_t width,
            uint32_t height,
            const MATH::Mat4& viewProj) {

            return
                IsHzbStatsUsable(stats, width, height) &&
                NearlyEqualMat4(stats.hzbViewProj, viewProj);
        }

        bool IsHistoryHzbUsableForCullingView(
            const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& stats,
            uint32_t width,
            uint32_t height,
            const MATH::Mat4& viewProj) {

            if (!IsHzbStatsUsable(stats, width, height)) {
                return false;
            }

            constexpr float kHistoryViewProjMaxDelta = 0.0125f;
            return MaxAbsDeltaMat4(stats.hzbViewProj, viewProj) <=
                kHistoryViewProjMaxDelta;
        }

        void ClearFrozenCullingDepthStats(ScreenSpaceRuntimeState& state) {
            state.frozenCullingDepthStats = {};
            state.frozenCullingDepthStatsValid = false;
        }
    }

    ScreenSpaceRuntimeState& GetScreenSpaceRuntimeState() {
        return gScreenSpaceState;
    }

    void ReleaseScreenSpaceRuntimeState() {
        gScreenSpaceState.geometryAux.Release();
        gScreenSpaceState.ssaoRenderer.Release();
        gScreenSpaceState.depthVisibility.Release();
        RENDER3D::ReleaseTextureResource(gScreenSpaceState.fallbackAoTextureResource);
        gScreenSpaceState.fallbackAoTextureResource = {};
        gScreenSpaceState.fallbackAoTextureHandle = -1;
        gScreenSpaceState.depthVisibilityValid = false;
        gScreenSpaceState.depthVisibilityViewProjValid = false;
        gScreenSpaceState.depthVisibilityBuildAllowedThisFrame = true;
        ClearFrozenCullingDepthStats(gScreenSpaceState);
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
        const MESHRENDERER::CameraCB& renderCameraCb,
        const MESHRENDERER::CameraCB& cullingCameraCb,
        const SceneEnvironment& environment) {

        ScreenSpaceFrameResult result{};
        const bool fallbackReady = EnsureScreenSpaceFallbacks(state);
        if (fallbackReady) {
            result.fallbackAoTextureResource = state.fallbackAoTextureResource;
            result.aoSrv = RENDER3D::GetTextureResourceSrvGpuHandle(state.fallbackAoTextureResource);
        }
        result.fallbackAoTextureHandle = state.fallbackAoTextureHandle;
        const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats historyDepthStats =
            state.depthVisibility.GetStats();
        const bool historyHzbReady =
            IsHzbStatsUsable(historyDepthStats, context.width, context.height);
        const bool historyHzbMatchesCullingView =
            IsHistoryHzbUsableForCullingView(
                historyDepthStats,
                context.width,
                context.height,
                cullingCameraCb.viewProj);
        state.depthVisibility.ResetFrame();
        BeginSsaoDebugFrame(context.width, context.height, environment.ambientOcclusion);

        if (context.cmd == nullptr) {
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            state.ssaoValid = false;
            return result;
        }

        // Screen-space pass は forward lighting の前に必要な texture を生成する。
        GFX::PIX::ScopedGpuEvent pixScreenSpace(context.cmd, GFX::PIX::kColorPost, "ScreenSpace.PreLighting");

        const SsaoMode ssaoMode = ResolveEffectiveSsaoMode(environment.ambientOcclusion);

        const CpuClock::time_point depthVisibilityStart = CpuClock::now();
        const bool frozenCullingView =
            MESHRENDERER::IsGpuDrivenCullingDebugFreezeActive();
        const bool depthVisibilityAllowedThisFrame = true;
        state.depthVisibilityBuildAllowedThisFrame =
            depthVisibilityAllowedThisFrame;
        if (frozenCullingView) {
            if (!state.frozenCullingDepthStatsValid &&
                IsHzbStatsUsableForView(
                    historyDepthStats,
                    context.width,
                    context.height,
                    cullingCameraCb.viewProj)) {
                state.frozenCullingDepthStats = historyDepthStats;
                state.frozenCullingDepthStatsValid = true;
            }

            const bool frozenHistoryReady =
                state.frozenCullingDepthStatsValid &&
                IsHzbStatsUsableForView(
                    state.frozenCullingDepthStats,
                    context.width,
                    context.height,
                    cullingCameraCb.viewProj);
            if (frozenHistoryReady) {
                GFX::PIX::ScopedGpuEvent pixFrozenHistory(
                    context.cmd,
                    GFX::PIX::kColorUpload,
                    "GpuDepthVisibility.UseFrozenHistoryHZB");
                result.hzbBuilt = true;
                state.depthVisibilityValid = true;
                state.depthVisibilityViewProjValid = true;
                state.depthVisibilityViewProj =
                    state.frozenCullingDepthStats.hzbViewProj;
                (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(
                    state.frozenCullingDepthStats);
            } else {
                D3D12_CPU_DESCRIPTOR_HANDLE visibilityDsv =
                    state.depthVisibility.BeginDepthPrepass(
                        context.cmd,
                        context.width,
                        context.height);
                const bool depthWritten =
                    visibilityDsv.ptr != 0 &&
                    MESHRENDERER::RenderDepthPrepass(visibilityDsv);
                result.depthPrepassWritten = depthWritten;
                state.depthVisibility.RecordDepthPrepass(depthWritten);
                if (depthWritten) {
                    result.hzbBuilt =
                        state.depthVisibility.BuildHzb(
                            context.cmd,
                            context.width,
                            context.height);
                    if (result.hzbBuilt) {
                        state.depthVisibility.RecordHzbViewProj(
                            cullingCameraCb.viewProj);
                    }
                }

                context.renderTargetAccess.Rebind();
                const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& currentDepthStats =
                    state.depthVisibility.GetStats();
                state.depthVisibilityValid =
                    result.hzbBuilt &&
                    currentDepthStats.hzbFinestSrv.ptr != 0 &&
                    currentDepthStats.hzbViewProjValid;
                state.depthVisibilityViewProjValid = state.depthVisibilityValid;
                state.depthVisibilityViewProj = cullingCameraCb.viewProj;
                if (state.depthVisibilityValid) {
                    state.frozenCullingDepthStats = currentDepthStats;
                    state.frozenCullingDepthStatsValid = true;
                    (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(
                        currentDepthStats);
                } else {
                    ClearFrozenCullingDepthStats(state);
                    (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth();
                }
            }
        } else if (depthVisibilityAllowedThisFrame && historyHzbReady && historyHzbMatchesCullingView) {
            ClearFrozenCullingDepthStats(state);
            GFX::PIX::ScopedGpuEvent pixHistory(
                context.cmd,
                GFX::PIX::kColorUpload,
                "GpuDepthVisibility.UseHistoryHZB");
            state.depthVisibilityValid = true;
            state.depthVisibilityViewProjValid = true;
            state.depthVisibilityViewProj = historyDepthStats.hzbViewProj;
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(
                historyDepthStats);
        } else {
            ClearFrozenCullingDepthStats(state);
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth();
        }

        const float depthVisibilityMs =
            ElapsedMs(depthVisibilityStart, CpuClock::now());

        // Off 時は GeometryAux も作らない。
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

        if (ssaoMode == SsaoMode::Balanced) {
            RecordSsaoGeometryAuxDebug(
                false,
                depthVisibilityMs,
                DXGI_FORMAT_UNKNOWN);
            state.geometryValid = false;

            if (state.ssaoValid &&
                state.ssaoRenderer.IsValid() &&
                state.ssaoRenderer.GetAoSrv().ptr != 0) {
                result.ssaoRendered = true;
                result.aoSrv = state.ssaoRenderer.GetAoSrv();
                GFX::PIX::SetGpuMarker(context.cmd, GFX::PIX::kColorPost, "SSAO.Composite");
                RecordSsaoCompositeDebug(0.0f);
            } else {
                state.ssaoValid = false;
                result.ssaoRendered = false;
            }
            return result;
        }

        GFX::PIX::ScopedGpuEvent pixGeometry(context.cmd, GFX::PIX::kColorRender, "GeometryAux");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuGeometry(
            context.cmd,
            GFX::GPU_PROFILE::Pass::GeometryAux);
        const CpuClock::time_point geometryStart = CpuClock::now();
        result.geometryAuxWritten = MESHRENDERER::RenderGeometryAuxPass(
            state.geometryAux,
            context.sceneDsv);
        context.renderTargetAccess.Rebind();
        RecordSsaoGeometryAuxDebug(
            result.geometryAuxWritten,
            ElapsedMs(geometryStart, CpuClock::now()),
            state.geometryAux.GetFormat());

        state.geometryValid = result.geometryAuxWritten && state.geometryAux.IsValid();
        if (!state.geometryValid ||
            !context.depthReadable ||
            context.sceneDepthSrv.ptr == 0) {
            state.ssaoValid = false;
            return result;
        }

        bool ssaoOk = false;
        if (context.renderTargetAccess.BeginDepthRead()) {
            ssaoOk = state.ssaoRenderer.Render(
                context.cmd,
                state.geometryAux,
                context.sceneDepthSrv,
                renderCameraCb,
                environment.ambientOcclusion);
            context.renderTargetAccess.EndDepthRead();
            // SSAO は内部 AO RT を複数回 bind するので、lighting pass の前に scene RT へ戻す。
            context.renderTargetAccess.Rebind();
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

    bool ExecuteScreenSpacePostOpaquePasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& cameraCb,
        const SceneEnvironment& environment,
        ScreenSpaceFrameResult& result) {

        if (context.cmd == nullptr) {
            return false;
        }

        const bool depthAvailable =
            context.depthReadable &&
            context.sceneDepthSrv.ptr != 0;
        if (!depthAvailable) {
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            return context.renderTargetAccess.Rebind();
        }

        const SsaoMode ssaoMode =
            ResolveEffectiveSsaoMode(environment.ambientOcclusion);
        const bool refreshBalancedSsao =
            ssaoMode == SsaoMode::Balanced &&
            !environment.ambientOcclusion.editorViewportSuppressed;

        if (!context.renderTargetAccess.BeginDepthRead()) {
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            return false;
        }

        {
            GFX::PIX::ScopedGpuEvent pixPostOpaque(
                context.cmd,
                GFX::PIX::kColorPost,
                "ScreenSpace.PostOpaqueTemporal");

            if (!MESHRENDERER::IsGpuDrivenCullingDebugFreezeActive() &&
                state.depthVisibilityBuildAllowedThisFrame) {
                result.hzbBuilt = state.depthVisibility.BuildHzbFromDepthSrv(
                    context.cmd,
                    context.width,
                    context.height,
                    context.sceneDepthSrv,
                    cameraCb.viewProj);
                if (result.hzbBuilt) {
                    state.depthVisibilityValid = true;
                    state.depthVisibilityViewProjValid = true;
                    state.depthVisibilityViewProj = cameraCb.viewProj;
                } else {
                    state.depthVisibilityValid = false;
                    state.depthVisibilityViewProjValid = false;
                }
            } else if (!state.depthVisibilityBuildAllowedThisFrame) {
                state.depthVisibilityValid = false;
                state.depthVisibilityViewProjValid = false;
            }

            if (refreshBalancedSsao) {
                const bool ssaoOk = state.ssaoRenderer.RenderDepthOnly(
                    context.cmd,
                    context.width,
                    context.height,
                    context.sceneDepthSrv,
                    cameraCb,
                    environment.ambientOcclusion);
                state.ssaoValid = ssaoOk;
                if (ssaoOk && state.ssaoRenderer.GetAoSrv().ptr != 0) {
                    result.ssaoRendered = true;
                    result.aoSrv = state.ssaoRenderer.GetAoSrv();
                    GFX::PIX::SetGpuMarker(
                        context.cmd,
                        GFX::PIX::kColorPost,
                        "SSAO.Composite");
                    RecordSsaoCompositeDebug(0.0f);
                }
            }
        }

        context.renderTargetAccess.EndDepthRead();
        return context.renderTargetAccess.Rebind();
    }

} // namespace HIKARI::RENDER3D::SCREENSPACE
