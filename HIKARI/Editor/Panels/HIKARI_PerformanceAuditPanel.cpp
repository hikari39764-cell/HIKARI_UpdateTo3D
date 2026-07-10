#include "HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include <algorithm>
#include <cstdint>
#include <string>

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
    namespace {

        struct RuntimePerformanceSnapshot {
            float fpsRaw = 0.0f;
            RenderSubmissionDebugStats submission{};
            RENDER3D::RUNTIME::SceneRenderCache::Stats scene{};
            RENDER3D::GPUDRIVEN::GpuSceneRegistryStats gpuRegistry{};
            MESHRENDERER::MeshRendererDebugStats mesh{};
            SHADOW::ShadowMapDebugStats shadow{};
            RENDER3D::ClusterGeometryResourceSystemStats clusterResources{};
            RENDER3D::RenderResourceDescriptorPoolStats descriptorPool{};
            RENDER3D::SCREENSPACE::SsaoDebugState ssao{};
            RENDER3D::SCREENSPACE::DepthVisibilityDebugState depthVisibility{};
            RENDER3D::TEMPORAL::TemporalFrameState temporalFrame{};
            RENDER3D::TEMPORAL::TemporalResourceStats temporalResources{};
            RENDER3D::UPSCALING::StreamlineDebugStats streamline{};
            GFX::GPU_PROFILE::FrameSnapshot gpu{};
            GFX::GPU_PIPELINE_STATS::FrameSnapshot pipelineStats{};
        };

        bool IsClusterSubpass(GFX::GPU_PROFILE::Pass pass) {
            return
                pass == GFX::GPU_PROFILE::Pass::ClusterCull ||
                pass == GFX::GPU_PROFILE::Pass::TraditionalDrawGeometryAux ||
                pass == GFX::GPU_PROFILE::Pass::TraditionalDrawForward ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthPrepass ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawForward ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthAware ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawTransparent;
        }

        bool IsShadowSubpass(GFX::GPU_PROFILE::Pass pass) {
            return
                pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadow ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadow ||
                pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadowStatic ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic ||
                pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadowDynamic ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic ||
                pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback;
        }

        bool IsNestedGpuSubpass(GFX::GPU_PROFILE::Pass pass) {
            return IsClusterSubpass(pass) || IsShadowSubpass(pass);
        }

        const char* TimingScopeText(GFX::GPU_PROFILE::Pass pass) {
            if (IsShadowSubpass(pass)) {
                return "Shadow Subpass";
            }
            return IsClusterSubpass(pass) ? "Cluster Subpass" : "Parent Pass";
        }

        const char* ReadyText(bool value) {
            return value ? "Ready" : "Missing";
        }

        double SafeDivide(uint64_t numerator, uint64_t denominator) {
            return denominator != 0u
                ? static_cast<double>(numerator) / static_cast<double>(denominator)
                : 0.0;
        }

        bool IsMeshletPipelineStatsPass(GFX::GPU_PROFILE::Pass pass) {
            return
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthPrepass ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawForward ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthAware ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawTransparent ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadow ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback;
        }

        void AppendShadowCacheReason(
            std::string& text,
            uint32_t flags,
            uint32_t bit,
            const char* label) {

            if ((flags & bit) == 0u) {
                return;
            }
            if (!text.empty()) {
                text += "+";
            }
            text += label;
        }

        std::string ShadowCacheMissReasonText(uint32_t flags) {
            if (flags == SHADOW::ShadowCacheMissReasonNone) {
                return "none";
            }
            std::string text{};
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonNoStaticWork, "no static work");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonStaticDirty, "static dirty");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonMatrix, "matrix");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonSource, "source");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonLayout, "layout");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonInstanceCount, "instance count");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonResolution, "resolution");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonResource, "resource");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonState, "state");
            AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonInvalid, "invalid");
            return text.empty() ? "mixed" : text;
        }

        ImVec4 StatusColor(bool ok) {
            return ok ?
                ImVec4(0.28f, 0.82f, 0.45f, 1.0f) :
                ImVec4(0.95f, 0.34f, 0.32f, 1.0f);
        }

        void TextStatus(const char* label, bool ok) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::TextColored(StatusColor(ok), "%s", ok ? "Ready" : "Missing");
        }

        template <typename... Args>
        void MetricRow(const char* label, const char* fmt, Args... args) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(fmt, args...);
        }

        void MetricRowText(const char* label, const char* value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(value);
        }

        bool BeginMetricTable(const char* id, float labelWidth = 210.0f) {
            if (!ImGui::BeginTable(
                id,
                2,
                ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp)) {
                return false;
            }
            ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthFixed, labelWidth);
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();
            return true;
        }

        RuntimePerformanceSnapshot BuildSnapshot() {
            RuntimePerformanceSnapshot out{};
            const FrameContext& frame = TIME::GetFrameContext();
            out.fpsRaw = frame.rawDt > 0.0f ? 1.0f / frame.rawDt : 0.0f;
            out.submission = RenderSubmissionSystem::GetDebugStats();
            out.scene = RenderSubmissionSystem::GetSceneRenderCacheStats();
            out.gpuRegistry = RenderSubmissionSystem::GetGpuSceneRegistryStats();
            out.mesh = MESHRENDERER::GetDebugStats();
            out.shadow = SHADOW::GetDebugStats();
            out.clusterResources = RENDER3D::GetClusterGeometryResourceSystemStats();
            out.descriptorPool = RENDER3D::GetRenderResourceDescriptorPoolStats();
            out.ssao = RENDER3D::SCREENSPACE::GetSsaoDebugState();
            out.depthVisibility =
                RENDER3D::SCREENSPACE::GetDepthVisibilityDebugState();
            out.temporalFrame =
                RENDER3D::TEMPORAL::GetCurrentTemporalFrameState();
            out.temporalResources =
                RENDER3D::TEMPORAL::GetTemporalResourceStats();
            out.streamline =
                RENDER3D::UPSCALING::GetStreamlineDebugStats();
            out.gpu = GFX::GPU_PROFILE::GetLatestSnapshot();
            out.pipelineStats = GFX::GPU_PIPELINE_STATS::GetLatestSnapshot();
            return out;
        }

        double SumGpuMsByScope(
            const GFX::GPU_PROFILE::FrameSnapshot& profile,
            bool nestedSubpass) {

            double total = 0.0;
            for (size_t i = 0; i < profile.passes.size(); ++i) {
                const GFX::GPU_PROFILE::Pass pass =
                    static_cast<GFX::GPU_PROFILE::Pass>(i);
                const GFX::GPU_PROFILE::PassTiming& timing = profile.passes[i];
                if (timing.valid && IsNestedGpuSubpass(pass) == nestedSubpass) {
                    total += timing.gpuMs;
                }
            }
            return total;
        }

        uint32_t CountValidGpuPasses(const GFX::GPU_PROFILE::FrameSnapshot& profile) {
            uint32_t count = 0;
            for (const GFX::GPU_PROFILE::PassTiming& timing : profile.passes) {
                if (timing.valid) {
                    ++count;
                }
            }
            return count;
        }

        void DrawFrameSummary(const RuntimePerformanceSnapshot& s) {
            const size_t mainlineSubmittedCount =
                s.mesh.meshletBackendSubmittedDispatchCount > 0
                    ? s.mesh.meshletBackendSubmittedDispatchCount
                    : s.mesh.clusterGpuCullGpuDrawCommandCount;
            const bool clusterMainline =
                s.mesh.clusterGpuCullSubmittedInstanceCount > 0 ||
                mainlineSubmittedCount > 0;
            const bool gpuDrivenReady =
                s.mesh.surfaceGpuSceneSrvValid &&
                s.mesh.surfaceGpuSceneBufferReady &&
                s.mesh.traditionalCommandStreamArgumentBufferReady &&
                s.mesh.traditionalCommandStreamCommandSignatureReady;
            const double parentGpuMs = SumGpuMsByScope(s.gpu, false);
            const double nestedSubpassGpuMs = SumGpuMsByScope(s.gpu, true);

            if (ImGui::BeginTable("GpuDrivenFrameSummary", 4, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                ImGui::Text("FPS %.1f", s.fpsRaw);
                ImGui::TextDisabled("route %s", ToString(s.submission.routeMode));

                ImGui::TableNextColumn();
                ImGui::TextColored(StatusColor(gpuDrivenReady), "GPU scene %s", ReadyText(gpuDrivenReady));
                ImGui::Text("uploaded %zu / requested %zu",
                    s.mesh.surfaceGpuSceneUploadedInstanceCount,
                    s.mesh.surfaceGpuSceneRequestedInstanceCount);

                ImGui::TableNextColumn();
                ImGui::TextColored(StatusColor(clusterMainline), "cluster %s", clusterMainline ? "Active" : "Idle");
                ImGui::Text("submitted %zu / eligible %zu",
                    mainlineSubmittedCount,
                    s.mesh.clusterGpuCullGpuDrawCommandCount);

                ImGui::TableNextColumn();
                ImGui::Text("GPU parent %.3f ms", parentGpuMs);
                ImGui::Text("nested sub %.3f ms", nestedSubpassGpuMs);
                ImGui::EndTable();
            }
        }

        void DrawGpuTimingTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("GPU Timing");
            if (!s.gpu.gpuTimingAvailable) {
                ImGui::TextDisabled("%s",
                    s.gpu.profilerEnabled ?
                        "Timestamp query data is waiting." :
                        s.gpu.unavailableReason);
                return;
            }

            const double parentMs = SumGpuMsByScope(s.gpu, false);
            const double nestedSubpassMs = SumGpuMsByScope(s.gpu, true);
            ImGui::Text("Frame %llu, passes %u, parent %.3f ms, nested subpass %.3f ms",
                static_cast<unsigned long long>(s.gpu.frameIndex),
                CountValidGpuPasses(s.gpu),
                parentMs,
                nestedSubpassMs);

            if (ImGui::BeginTable(
                    "GpuDrivenTimingTable",
                    5,
                    ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Share");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < s.gpu.passes.size(); ++i) {
                    const GFX::GPU_PROFILE::Pass pass =
                        static_cast<GFX::GPU_PROFILE::Pass>(i);
                    const GFX::GPU_PROFILE::PassTiming& timing = s.gpu.passes[i];
                    if (!timing.valid) {
                        continue;
                    }

                    const bool nestedSubpass = IsNestedGpuSubpass(pass);
                    const double scopeTotal =
                        (std::max)(0.0001, nestedSubpass ? nestedSubpassMs : parentMs);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(timing.name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(TimingScopeText(pass));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", timing.gpuMs);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%llu", static_cast<unsigned long long>(timing.ticks));
                    ImGui::TableSetColumnIndex(4);
                    ImGui::ProgressBar(
                        static_cast<float>(timing.gpuMs / scopeTotal),
                        ImVec2(-1.0f, 0.0f),
                        "");
                }
                ImGui::EndTable();
            }
        }

        void DrawRenderPathTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Render Path");
            if (BeginMetricTable("RenderPathMetrics", 250.0f)) {
                MetricRowText("Route", ToString(s.submission.routeMode));
                MetricRow("Mainline / Strict / CPU Views Suppressed", "%s / %s / %u",
                    s.submission.gpuDrivenMainRouteActive ? "on" : "off",
                    s.gpuRegistry.strictGpuDrivenMainline ? "on" : "off",
                    s.gpuRegistry.cpuForwardViewSuppressedCount);
                MetricRow("Scene Scanned / Submitted / Hidden", "%d / %d / %d",
                    s.submission.scannedModelCount,
                    s.submission.submittedModelCount,
                    s.submission.hiddenModelCount);
                MetricRow("Surfaces Total / Clustered / Dirty", "%u / %u / %u",
                    s.scene.surfaceInstanceCount,
                    s.scene.clusteredGeometrySurfaceInstanceCount,
                    s.scene.dirtySurfaceInstanceCount);
                MetricRow("GPU Records Source / Routed / Unsupported", "%u / %u / %u",
                    s.gpuRegistry.sourceRecordCount,
                    s.gpuRegistry.forwardRoutedRecordCount,
                    s.gpuRegistry.unsupportedForwardRecordCount);
                MetricRow("GPU Scene Upload Full / Dirty / Reuse", "%zu / %zu / %zu",
                    s.mesh.surfaceGpuSceneFullUploadCount,
                    s.mesh.surfaceGpuSceneDirtyPatchCount,
                    s.mesh.surfaceGpuSceneReuseCount);
                MetricRow("GPU Scene Instances Opaque / DepthPre / Shadow", "%u / %u / %zu",
                    s.gpuRegistry.forwardOpaqueGpuSceneStats.instanceCount,
                    s.gpuRegistry.depthPrepassGpuSceneStats.instanceCount,
                    s.shadow.shadowGpuSceneUploadedInstanceCount);
                MetricRow("Command Stream GPU / Traditional / Overflow", "%zu / %zu / %zu",
                    s.mesh.gpuDrivenCommandStreamGpuCommandCount,
                    s.mesh.gpuDrivenCommandStreamTraditionalCommandCount,
                    s.mesh.traditionalCommandStreamOverflowCommandCount);
                ImGui::EndTable();
            }
        }

        void DrawMeshShaderPipelineStatsTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Mesh Shader Pipeline Stats");
            if (!s.pipelineStats.pipelineStatsAvailable) {
                const char* reason =
                    s.pipelineStats.unavailableReason != nullptr &&
                            s.pipelineStats.unavailableReason[0] != '\0'
                        ? s.pipelineStats.unavailableReason
                        : "Pipeline statistics query data is waiting.";
                ImGui::TextDisabled("%s", reason);
                return;
            }

            bool drewRows = false;
            if (ImGui::BeginTable(
                    "MeshShaderPipelineStatsTable",
                    8,
                    ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("AS", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("MS", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("MS Prim", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Clip Prim", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("PS", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Prim / MS", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("PS / Prim");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < s.pipelineStats.passes.size(); ++i) {
                    const GFX::GPU_PROFILE::Pass pass =
                        static_cast<GFX::GPU_PROFILE::Pass>(i);
                    if (!IsMeshletPipelineStatsPass(pass)) {
                        continue;
                    }

                    const GFX::GPU_PIPELINE_STATS::PassPipelineStats& stats =
                        s.pipelineStats.passes[i];
                    if (!stats.valid) {
                        continue;
                    }
                    drewRows = true;

                    const D3D12_QUERY_DATA_PIPELINE_STATISTICS1& c = stats.counters;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(stats.name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", static_cast<unsigned long long>(c.ASInvocations));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", static_cast<unsigned long long>(c.MSInvocations));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%llu", static_cast<unsigned long long>(c.MSPrimitives));
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llu", static_cast<unsigned long long>(c.CPrimitives));
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%llu", static_cast<unsigned long long>(c.PSInvocations));
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%.2f", SafeDivide(c.MSPrimitives, c.MSInvocations));
                    ImGui::TableSetColumnIndex(7);
                    ImGui::Text("%.2f", SafeDivide(c.PSInvocations, c.MSPrimitives));
                }
                ImGui::EndTable();
            }
            if (!drewRows) {
                ImGui::TextDisabled("No meshlet draw pipeline statistics were captured in the latest resolved frame.");
            }
        }

        void DrawGeometryPipelineTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Geometry Pipeline");
            if (BeginMetricTable("GeometryPipelineMetrics", 250.0f)) {
                MetricRow("Cluster Cull Source / Submitted / Overflow", "%zu / %zu / %zu",
                    s.mesh.clusterGpuCullSourceInstanceCount,
                    s.mesh.clusterGpuCullSubmittedInstanceCount,
                    s.mesh.clusterGpuCullOverflowInstanceCount);
                MetricRow("Visible Ranges / Clusters / Draw Args", "%zu / %zu / %zu",
                    s.mesh.clusterGpuCullGpuVisibleRangeCount,
                    s.mesh.clusterGpuCullGpuVisibleClusterCount,
                    s.mesh.clusterGpuCullGpuDrawCommandCount);
                MetricRow("Culling Camera Frozen / HZB Used", "%s / %s",
                    s.mesh.gpuDrivenCullingCameraFrozen ? "yes" : "no",
                    s.depthVisibility.visibilityUsedHzb ? "yes" : "no");
                MetricRow("HZB Visibility / Pyramid / History", "%s / %s / %s %s",
                    RENDER3D::SCREENSPACE::ToString(
                        s.depthVisibility.visibilitySource),
                    RENDER3D::SCREENSPACE::ToString(
                        s.depthVisibility.latestPyramidSource),
                    s.depthVisibility.historyReady ? "ready" : "cold",
                    s.depthVisibility.historyMatched ? "matched" : "moved");
                MetricRow("HZB Enabled / Size / Budget Skipped", "%s / %zu x %zu / %zu",
                    s.mesh.clusterGpuCullHzbOcclusionEnabled ? "yes" : "no",
                    s.mesh.clusterGpuCullHzbOcclusionWidth,
                    s.mesh.clusterGpuCullHzbOcclusionHeight,
                    s.mesh.clusterGpuCullGpuHzbBudgetSkippedCount);
                MetricRow("HZB Page Tested / Culled | Cluster Tested / Culled", "%zu / %zu | %zu / %zu",
                    s.mesh.clusterGpuCullGpuPageOcclusionTestedCount,
                    s.mesh.clusterGpuCullGpuPageOcclusionCulledCount,
                    s.mesh.clusterGpuCullGpuClusterOcclusionTestedCount,
                    s.mesh.clusterGpuCullGpuClusterOcclusionCulledCount);
                MetricRow("HZB Raw / Confirmed / Pending / Reset", "%zu / %zu / %zu / %zu",
                    s.mesh.clusterGpuCullGpuHzbRawOccludedCount,
                    s.mesh.clusterGpuCullGpuHzbTemporalConfirmedCount,
                    s.mesh.clusterGpuCullGpuHzbTemporalPendingCount,
                    s.mesh.clusterGpuCullGpuHzbTemporalResetCount);
                MetricRow("GPU LOD Selected L0 / L1 / L2 / L3+", "%zu / %zu / %zu / %zu",
                    s.mesh.clusterGpuCullGpuLod0SelectedCount,
                    s.mesh.clusterGpuCullGpuLod1SelectedCount,
                    s.mesh.clusterGpuCullGpuLod2SelectedCount,
                    s.mesh.clusterGpuCullGpuLod3PlusSelectedCount);
                MetricRow("LOD Policy Error / Radius Relax / Error Relax", "%.4f / %.2f / %.2f",
                    s.mesh.clusterGpuCullLodTargetErrorNdc,
                    s.mesh.clusterGpuCullLodTransitionRelaxPerLevel,
                    s.mesh.clusterGpuCullLodErrorRelaxPerLevel);
                MetricRow("Meshlet Dispatch Requested / Submitted / Calls / Empty", "%zu / %zu / %zu / %zu",
                    s.mesh.meshletBackendRequestedDispatchCount,
                    s.mesh.meshletBackendSubmittedDispatchCount,
                    s.mesh.meshletBackendSubmitCallCount,
                    s.mesh.meshletBackendSkippedBucketCount);
                MetricRow("Meshlet Passes Forward / DepthPre / Shadow / Geometry", "%zu / %zu / %zu / %zu",
                    s.mesh.meshletBackendForwardSubmittedDispatchCount,
                    s.mesh.meshletBackendDepthPrepassSubmittedDispatchCount,
                    s.mesh.meshletBackendShadowSubmittedDispatchCount,
                    s.mesh.meshletBackendGeometryAuxSubmittedDispatchCount);
                ImGui::EndTable();
            }
        }

        void DrawResourceSummaryTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Resources");
            if (BeginMetricTable("ClusterResourceMetrics")) {
                MetricRow("Context / Ready / Failed", "%s / %u / %u",
                    s.clusterResources.initialized ? "Ready" : "Missing",
                    s.clusterResources.readyResourceCount,
                    s.clusterResources.failedCount);
                MetricRow("Requests / Hits / Misses / Loaded", "%u / %u / %u / %u",
                    s.clusterResources.requestCount,
                    s.clusterResources.hitCount,
                    s.clusterResources.missCount,
                    s.clusterResources.loadedCount);
                MetricRow("Surfaces / LOD Ranges / Clusters / Pages / Ranges", "%u / %u / %u / %u / %u",
                    s.clusterResources.surfaceCount,
                    s.clusterResources.surfaceLodRangeCount,
                    s.clusterResources.clusterCount,
                    s.clusterResources.pageCount,
                    s.clusterResources.surfaceRangeCount);
                MetricRow("Vertices / Indices / Primitives / GPU Bytes", "%u / %u / %u / %.2f MB",
                    s.clusterResources.vertexCount,
                    s.clusterResources.indexCount,
                    s.clusterResources.meshletPrimitiveCount,
                    static_cast<double>(s.clusterResources.gpuBufferBytes) / (1024.0 * 1024.0));
                MetricRow("Shader SRV / Missing / Allocation Failed", "%u / %u / %u",
                    s.clusterResources.shaderVisibleResourceCount,
                    s.clusterResources.missingDescriptorCount,
                    s.clusterResources.descriptorAllocationFailedCount);
                MetricRow("Descriptor Pool Used / Capacity / Failed", "%u / %u / %u",
                    s.descriptorPool.used,
                    s.descriptorPool.capacity,
                    s.descriptorPool.failedAllocationCount);
                ImGui::EndTable();
            }
        }

        void DrawEffectsTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Effects");
            if (BeginMetricTable("EffectsMetrics", 250.0f)) {
                MetricRow("SSAO Mode / Valid / Half", "%s / %s / %s",
                    RENDER3D::SCREENSPACE::ToString(s.ssao.mode),
                    s.ssao.valid ? "yes" : "no",
                    s.ssao.halfResolution ? "yes" : "no");
                MetricRow("SSAO AO Size / Samples / Blur", "%u x %u / %u / %u",
                    s.ssao.internalWidth,
                    s.ssao.internalHeight,
                    s.ssao.sampleCount,
                    s.ssao.blurIterations);
                MetricRow("SSAO CPU Main / Blur / Total", "%.3f / %.3f / %.3f ms",
                    s.ssao.mainCpuMs,
                    s.ssao.blurCpuMs,
                    s.ssao.totalCpuMs);
                MetricRow("Shadow Enabled / Resolution / Ortho / Texel", "%s / %u / %.2f / %.5f",
                    s.shadow.enabled ? "yes" : "no",
                    s.shadow.resolution,
                    s.shadow.orthoSize,
                    s.shadow.worldTexelSize);
                const std::string currentMissReason =
                    ShadowCacheMissReasonText(s.shadow.shadowCacheMissReasonFlags);
                const std::string lastMissReason =
                    ShadowCacheMissReasonText(s.shadow.shadowCacheLastMissReasonFlags);
                MetricRow("Shadow Cache Valid / Hit / Miss / Last", "%s / %s / %s / %s",
                    s.shadow.shadowCacheValid ? "yes" : "no",
                    s.shadow.shadowCacheHit ? "yes" : "no",
                    currentMissReason.c_str(),
                    lastMissReason.c_str());
                MetricRow("Shadow Cache Hits / Misses / Copy / Update", "%zu / %zu / %zu / %zu",
                    s.shadow.shadowCacheHitCount,
                    s.shadow.shadowCacheMissCount,
                    s.shadow.shadowStaticCacheCopyCount,
                    s.shadow.shadowStaticCacheUpdateCount);
                MetricRow("Shadow Sources Static / Dynamic / Drawn", "%zu / %zu / %s %s %s",
                    s.shadow.shadowStaticSourceInstanceCount,
                    s.shadow.shadowDynamicSourceInstanceCount,
                    s.shadow.shadowStaticRendered ? "static" : "-",
                    s.shadow.shadowDynamicRendered ? "dynamic" : "-",
                    s.shadow.shadowFallbackRendered ? "fallback" : "-");
                MetricRow("Shadow Meshlet Dispatch Req / Submitted / Skip", "%zu / %zu / %zu",
                    s.shadow.shadowMeshletRequestedDispatchCount,
                    s.shadow.shadowMeshletSubmittedDispatchCount,
                    s.shadow.shadowMeshletSkippedDispatchCount);
                ImGui::EndTable();
            }
        }

        void DrawTemporalTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Temporal");
            if (BeginMetricTable("TemporalMetrics", 250.0f)) {
                const RENDER3D::RenderQualitySettings& renderQuality =
                    RENDER3D::GetRenderQualitySettings();
                MetricRow("AA Mode", "%s",
                    RENDER3D::RenderAntiAliasingModeLabel(
                        renderQuality.antiAliasingMode));
                MetricRow("Frame / History / Reset", "%llu / %s / %s",
                    static_cast<unsigned long long>(s.temporalFrame.frameIndex),
                    s.temporalFrame.historyValid ? "valid" : "cold",
                    RENDER3D::TEMPORAL::ToString(s.temporalFrame.resetReason));
                MetricRow("Render / Output Size", "%u x %u / %u x %u",
                    s.temporalFrame.renderWidth,
                    s.temporalFrame.renderHeight,
                    s.temporalFrame.outputWidth,
                    s.temporalFrame.outputHeight);
                MetricRow("Jitter / Phase / Pixels", "%s / %u / %.3f, %.3f",
                    s.temporalFrame.jitterEnabled ? "on" : "off",
                    s.temporalFrame.jitterPhase,
                    s.temporalFrame.camera.jitter.x,
                    s.temporalFrame.camera.jitter.y);
                MetricRow("Motion RG / Metadata / Written", "%s / %s / %s",
                    s.temporalResources.motionVectorReady ? "yes" : "no",
                    s.temporalResources.motionMetadataReady ? "yes" : "no",
                    s.temporalResources.motionVectorWritten ? "yes" : "no");
                MetricRow("Velocity Draws Rigid / Skinned / AlphaMask", "%u / %u / %u",
                    s.temporalResources.rigidVelocityDrawCount,
                    s.temporalResources.skinnedVelocityDrawCount,
                    s.temporalResources.alphaMaskedVelocityDrawCount);
                MetricRow("Scene Color / TAA", "%s / %s / %s",
                    s.temporalResources.sceneColorReady ? "yes" : "no",
                    s.temporalResources.taaEnabled ? "on" : "off",
                    s.temporalResources.taaResolved ? "resolved" : "skipped");
                MetricRow("History Color / Depth / Valid", "%s / %s / %s",
                    s.temporalResources.historyColorReady ? "yes" : "no",
                    s.temporalResources.historyDepthReady ? "yes" : "no",
                    (s.temporalResources.historyColorValid &&
                        s.temporalResources.historyDepthValid) ? "yes" : "no");
                MetricRow("TAA Resolved Ready", "%s",
                    s.temporalResources.taaResolvedColorReady ? "yes" : "no");
                MetricRow("Exposure Ready / Written", "%s / %s",
                    s.temporalResources.exposureReady ? "yes" : "no",
                    s.temporalResources.exposureWritten ? "yes" : "no");
                MetricRow("Masks R/T/Invalid Ready / Valid / Source", "%s / %s / %s",
                    (s.temporalResources.reactiveMaskReady &&
                        s.temporalResources.transparencyMaskReady &&
                        s.temporalResources.invalidDepthMotionMaskReady) ? "yes" : "no",
                    s.temporalResources.masksWritten ? "yes" : "no",
                    s.temporalResources.masksCompositionDerived
                        ? "composition-diff"
                        : "opaque-zero");
                MetricRow("Streamline SDK / Init / Device", "%s / %s / %s",
                    s.streamline.sdkCompiled ? "yes" : "no",
                    s.streamline.initialized ? "yes" : "no",
                    s.streamline.deviceAttached ? "yes" : "no");
                MetricRow("DLSS Support / Token / Constants", "%s / %s / %s",
                    s.streamline.dlssSupported ? "yes" : "no",
                    s.streamline.frameTokenReady ? "yes" : "no",
                    s.streamline.constantsSubmitted ? "yes" : "no");
                MetricRow("DLSS Mode / Requested / Evaluated", "%s / %s / %s",
                    RENDER3D::UPSCALING::ToString(s.streamline.mode),
                    s.streamline.dlssRequested ? "yes" : "no",
                    s.streamline.dlssEvaluated ? "yes" : "no");
                MetricRow("DLSS Fallback / Output / Evaluations", "%s / %s / %llu",
                    s.streamline.fallbackUsed ? "yes" : "no",
                    s.streamline.outputReady ? "ready" : "missing",
                    static_cast<unsigned long long>(s.streamline.evaluationCount));
                MetricRow("DLSS Failures / VRAM MB", "%llu / %.2f",
                    static_cast<unsigned long long>(s.streamline.failureCount),
                    static_cast<double>(s.streamline.estimatedVramBytes) /
                        (1024.0 * 1024.0));
                MetricRow("DLSS Render / Output", "%u x %u / %u x %u",
                    s.streamline.renderWidth,
                    s.streamline.renderHeight,
                    s.streamline.outputWidth,
                    s.streamline.outputHeight);
                MetricRow("DLSS Optimal / Min / Max", "%u x %u / %u x %u / %u x %u",
                    s.streamline.optimalRenderWidth,
                    s.streamline.optimalRenderHeight,
                    s.streamline.minRenderWidth,
                    s.streamline.minRenderHeight,
                    s.streamline.maxRenderWidth,
                    s.streamline.maxRenderHeight);
                MetricRow("Streamline Status / Last", "%s / %s: %s",
                    RENDER3D::UPSCALING::ToString(s.streamline.status),
                    s.streamline.lastOperation.empty()
                        ? "none"
                        : s.streamline.lastOperation.c_str(),
                    s.streamline.lastResult.empty()
                        ? "none"
                        : s.streamline.lastResult.c_str());
                MetricRow("Temporal Debug", "%s / %s",
                    ToString(s.temporalResources.debugView),
                    s.temporalResources.debugOutputReady ? "ready" : "missing");
                MetricRow("Invalidations / Resizes", "%llu / %llu",
                    static_cast<unsigned long long>(
                        s.temporalResources.historyInvalidationCount),
                    static_cast<unsigned long long>(
                        s.temporalResources.resourceResizeCount));
                ImGui::EndTable();
            }
        }

        void DrawReadiness(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Readiness");
            if (ImGui::BeginTable("ReadinessTable", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                TextStatus("GPU Scene", s.mesh.surfaceGpuSceneSrvValid && s.mesh.surfaceGpuSceneBufferReady);
                TextStatus("Meshlet Args",
                    s.mesh.meshletBackendDispatchArgumentBufferReady &&
                    s.mesh.meshletBackendDispatchCommandSignatureReady);
                ImGui::TableNextColumn();
                TextStatus("Cluster Cull", s.mesh.clusterGpuCullReady);
                TextStatus("Meshlet Draw",
                    s.mesh.meshletBackendPipelineReady &&
                    s.mesh.meshletBackendDispatchArgumentBufferReady &&
                    s.mesh.meshletBackendDispatchCommandSignatureReady);
                ImGui::TableNextColumn();
                TextStatus("Cluster Resource", s.clusterResources.initialized && s.clusterResources.readyResourceCount > 0);
                TextStatus("Meshlet Backend", s.mesh.meshletBackendPipelineReady);
                TextStatus("GPU Timing", s.gpu.gpuTimingAvailable);
                ImGui::EndTable();
            }
        }

    } // namespace
#endif

    void PerformanceAuditPanel::Draw(bool& open) const {
#if defined(HIKARI_WITH_EDITOR)
        if (!open) {
            return;
        }
        if (!ImGui::Begin("Performance Audit", &open)) {
            ImGui::End();
            return;
        }

        DrawContents();

        ImGui::End();
#else
        (void)open;
#endif
    }

    void PerformanceAuditPanel::DrawContents() const {
#if defined(HIKARI_WITH_EDITOR)
        const RuntimePerformanceSnapshot snapshot = BuildSnapshot();
        DrawFrameSummary(snapshot);
        DrawReadiness(snapshot);
        DrawGpuTimingTable(snapshot);
        DrawMeshShaderPipelineStatsTable(snapshot);
        DrawRenderPathTable(snapshot);
        DrawGeometryPipelineTable(snapshot);
        DrawTemporalTable(snapshot);
        DrawEffectsTable(snapshot);
        DrawResourceSummaryTable(snapshot);
#endif
    }

} // namespace HIKARI
