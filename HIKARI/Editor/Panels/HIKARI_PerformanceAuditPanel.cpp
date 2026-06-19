#include "HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

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
            RENDERER3D::DEBUG::DebugRendererFrameStats debugOverlay{};
            GFX::GPU_PROFILE::FrameSnapshot gpu{};
        };

        template <typename Numerator, typename Denominator>
        float SafeRatio(Numerator numerator, Denominator denominator) {
            const double safeDenominator = static_cast<double>(denominator);
            return safeDenominator > 0.0 ?
                static_cast<float>(static_cast<double>(numerator) / safeDenominator) :
                0.0f;
        }

        bool IsClusterSubpass(GFX::GPU_PROFILE::Pass pass) {
            return
                pass == GFX::GPU_PROFILE::Pass::ClusterCull ||
                pass == GFX::GPU_PROFILE::Pass::ClusterDrawGeometryAux ||
                pass == GFX::GPU_PROFILE::Pass::ClusterDrawForward ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux ||
                pass == GFX::GPU_PROFILE::Pass::MeshletDrawForward;
        }

        const char* TimingScopeText(GFX::GPU_PROFILE::Pass pass) {
            return IsClusterSubpass(pass) ? "Cluster Subpass" : "Parent Pass";
        }

        const char* ReadyText(bool value) {
            return value ? "Ready" : "Missing";
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
            out.debugOverlay = RENDERER3D::DEBUG::GetDebugRendererFrameStats();
            out.gpu = GFX::GPU_PROFILE::GetLatestSnapshot();
            return out;
        }

        double SumGpuMsByScope(
            const GFX::GPU_PROFILE::FrameSnapshot& profile,
            bool clusterSubpass) {

            double total = 0.0;
            for (size_t i = 0; i < profile.passes.size(); ++i) {
                const GFX::GPU_PROFILE::Pass pass =
                    static_cast<GFX::GPU_PROFILE::Pass>(i);
                const GFX::GPU_PROFILE::PassTiming& timing = profile.passes[i];
                if (timing.valid && IsClusterSubpass(pass) == clusterSubpass) {
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
                    : s.mesh.clusterDrawSubmittedCount;
            const bool clusterMainline =
                s.mesh.clusterGpuCullSubmittedInstanceCount > 0 ||
                mainlineSubmittedCount > 0;
            const bool gpuDrivenReady =
                s.mesh.surfaceGpuSceneSrvValid &&
                s.mesh.surfaceGpuSceneBufferReady &&
                s.mesh.surfaceIndirectArgumentBufferReady &&
                s.mesh.surfaceIndirectCommandSignatureReady;
            const double parentGpuMs = SumGpuMsByScope(s.gpu, false);
            const double clusterSubpassGpuMs = SumGpuMsByScope(s.gpu, true);

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
                    s.mesh.clusterDrawEligibleCommandCount);

                ImGui::TableNextColumn();
                ImGui::Text("GPU parent %.3f ms", parentGpuMs);
                ImGui::Text("cluster sub %.3f ms", clusterSubpassGpuMs);
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
            const double clusterSubpassMs = SumGpuMsByScope(s.gpu, true);
            ImGui::Text("Frame %llu, passes %u, parent %.3f ms, cluster subpass %.3f ms",
                static_cast<unsigned long long>(s.gpu.frameIndex),
                CountValidGpuPasses(s.gpu),
                parentMs,
                clusterSubpassMs);

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

                    const bool clusterSubpass = IsClusterSubpass(pass);
                    const double scopeTotal =
                        (std::max)(0.0001, clusterSubpass ? clusterSubpassMs : parentMs);

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

        void DrawClusterRuntimeTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Cluster Runtime");
            if (BeginMetricTable("ClusterRuntimeMetrics")) {
                MetricRow("GPU Cull Instances Source / Submitted / Overflow", "%zu / %zu / %zu",
                    s.mesh.clusterGpuCullSourceInstanceCount,
                    s.mesh.clusterGpuCullSubmittedInstanceCount,
                    s.mesh.clusterGpuCullOverflowInstanceCount);
                MetricRow("GPU PageTasks GPUScene Seeds / GPU Expanded / Overflow", "%zu / %zu / %zu",
                    s.mesh.clusterGpuCullSourcePageTaskCount,
                    s.mesh.clusterGpuCullGpuPageTaskCount,
                    s.mesh.clusterGpuCullGpuPageTaskOverflowCount);
                MetricRow("GPU Driven Worklist Passes / Cluster / Instances / Cluster Inst", "%zu / %zu / %zu / %zu",
                    s.mesh.gpuDrivenWorklistPassCount,
                    s.mesh.gpuDrivenWorklistClusterPassCount,
                    s.mesh.gpuDrivenWorklistSourceInstanceCount,
                    s.mesh.gpuDrivenWorklistClusterInstanceCount);
                MetricRow("GPU Driven CommandStream Passes / Ranges / GPU Cmd / CPU Cmd", "%zu / %zu / %zu / %zu",
                    s.mesh.gpuDrivenCommandStreamPassCount,
                    s.mesh.gpuDrivenCommandStreamRangeCount,
                    s.mesh.gpuDrivenCommandStreamGpuCommandCount,
                    s.mesh.gpuDrivenCommandStreamCpuCommandCount);
                MetricRow("GPU Visibility Counter Ranges / Known Visible / Known Overflow", "%zu / %zu / %zu",
                    s.mesh.gpuDrivenCommandStreamGpuCounterBackedRangeCount,
                    s.mesh.gpuDrivenCommandStreamKnownVisibleCommandCount,
                    s.mesh.gpuDrivenCommandStreamKnownVisibleCommandOverflowCount);
                MetricRow("Legacy Fallback Calls / Items / Opaque / Depth / Transparent / Geometry", "%zu / %zu / %zu / %zu / %zu / %zu",
                    s.mesh.legacyFallbackInvocationCount,
                    s.mesh.legacyFallbackItemCount,
                    s.mesh.legacyFallbackOpaqueItemCount,
                    s.mesh.legacyFallbackDepthAwareItemCount,
                    s.mesh.legacyFallbackTransparentItemCount,
                    s.mesh.legacyFallbackGeometryAuxItemCount);
                MetricRow("Visible Runs / Input Culled / Clusters / DrawArgs / Overflow", "%zu / %zu / %zu / %zu / %zu",
                    s.mesh.clusterGpuCullGpuVisibleRangeCount,
                    s.mesh.clusterGpuCullGpuInputFrustumCulledCount,
                    s.mesh.clusterGpuCullGpuVisibleClusterCount,
                    s.mesh.clusterGpuCullGpuDrawCommandCount,
                    s.mesh.clusterGpuCullGpuDrawCommandOverflowCount + s.mesh.clusterGpuCullGpuOverflowCount);
                MetricRow("Page Tested / Page Frustum Culled", "%zu / %zu",
                    s.mesh.clusterGpuCullGpuPageTestedCount,
                    s.mesh.clusterGpuCullGpuPageFrustumCulledCount);
                MetricRow("Cluster Tested / Frustum Culled / Cone Culled", "%zu / %zu / %zu",
                    s.mesh.clusterGpuCullGpuClusterTestedCount,
                    s.mesh.clusterGpuCullGpuClusterFrustumCulledCount,
                    s.mesh.clusterGpuCullGpuClusterConeCulledCount);
                MetricRow("DrawArgs BackFace / DoubleSided / DoubleSided Share", "%zu / %zu / %.1f%%",
                    s.mesh.clusterGpuCullGpuBackFaceDrawCommandCount,
                    s.mesh.clusterGpuCullGpuDoubleSidedDrawCommandCount,
                    SafeRatio(
                        s.mesh.clusterGpuCullGpuDoubleSidedDrawCommandCount,
                        s.mesh.clusterGpuCullGpuDrawCommandCount) * 100.0);
                MetricRow("Batch Quality ClustersPerDraw / RangesPerDraw / MergeGaps", "%.2f / %.2f / %zu",
                    SafeRatio(s.mesh.clusterGpuCullGpuVisibleClusterCount, s.mesh.clusterGpuCullGpuDrawCommandCount),
                    SafeRatio(s.mesh.clusterGpuCullGpuVisibleRangeCount, s.mesh.clusterGpuCullGpuDrawCommandCount),
                    s.mesh.clusterGpuCullGpuMergedGapCount);
                MetricRow("GPU LOD Selected L0 / L1 / L2 / L3+", "%zu / %zu / %zu / %zu",
                    s.mesh.clusterGpuCullGpuLod0SelectedCount,
                    s.mesh.clusterGpuCullGpuLod1SelectedCount,
                    s.mesh.clusterGpuCullGpuLod2SelectedCount,
                    s.mesh.clusterGpuCullGpuLod3PlusSelectedCount);
                MetricRow("Mainline Ready / Seeds / OverflowBlock", "%s / %s / %s",
                    s.mesh.clusterMainlineReady ? "Ready" : "Blocked",
                    s.mesh.clusterMainlineHasDrawSeeds ? "yes" : "no",
                    s.mesh.clusterMainlineOverflowBlocked ? "yes" : "no");
                MetricRow("Meshlet ExecuteIndirect Calls BackFace / DoubleSided / EmptyBuckets", "%zu / %zu / %zu",
                    s.mesh.meshletBackendBackFaceSubmitCallCount,
                    s.mesh.meshletBackendDoubleSidedSubmitCallCount,
                    s.mesh.meshletBackendSkippedBucketCount);
                MetricRow("Meshlet Ranges Forward / Geometry / Requested", "%zu / %zu / %zu",
                    s.mesh.meshletBackendForwardSubmittedDispatchCount,
                    s.mesh.meshletBackendGeometryAuxSubmittedDispatchCount,
                    s.mesh.meshletBackendRequestedDispatchCount);
                ImGui::EndTable();
            }
        }

        void DrawStrictGpuDrivenTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Strict GPU Driven");
            if (BeginMetricTable("StrictGpuDrivenMetrics", 270.0f)) {
                const bool cpuAuthoredClear =
                    s.mesh.gpuDrivenCommandStreamCpuCommandCount == 0 &&
                    s.mesh.gpuDrivenCommandStreamTraditionalCommandCount == 0 &&
                    s.mesh.legacyFallbackInvocationCount == 0 &&
                    s.mesh.surfaceIndirectCpuDirectCommandCount == 0;
                MetricRow("Strict Mainline / Legacy Views Suppressed", "%s / %u",
                    s.gpuRegistry.strictGpuDrivenMainline ? "on" : "off",
                    s.gpuRegistry.legacyForwardViewSuppressedCount);
                MetricRow("Strict Mainline Blocked Records", "%u",
                    s.gpuRegistry.strictMainlineBlockedRecordCount);
                MetricRow("Blocked Records Depth / Transparent / Shadow", "%u / %u / %u",
                    s.gpuRegistry.blockedForwardDepthAwareRecordCount,
                    s.gpuRegistry.blockedForwardTransparentRecordCount,
                    s.gpuRegistry.blockedShadowRecordCount);
                MetricRow("CommandStream GPU / CPU / Traditional", "%zu / %zu / %zu",
                    s.mesh.gpuDrivenCommandStreamGpuCommandCount,
                    s.mesh.gpuDrivenCommandStreamCpuCommandCount,
                    s.mesh.gpuDrivenCommandStreamTraditionalCommandCount);
                MetricRow("Legacy Fallback Calls / Items", "%zu / %zu",
                    s.mesh.legacyFallbackInvocationCount,
                    s.mesh.legacyFallbackItemCount);
                MetricRow("SurfaceIndirect Uploaded / CPU Direct / Overflow", "%zu / %zu / %zu",
                    s.mesh.surfaceIndirectUploadedCommandCount,
                    s.mesh.surfaceIndirectCpuDirectCommandCount,
                    s.mesh.surfaceIndirectOverflowCommandCount);
                MetricRow("GPU Scene Instances Opaque / Depth / Transparent / Shadow", "%u / %u / %u / %zu",
                    s.gpuRegistry.forwardOpaqueGpuSceneStats.instanceCount,
                    s.gpuRegistry.forwardDepthAwareGpuSceneStats.instanceCount,
                    s.gpuRegistry.forwardTransparentGpuSceneStats.instanceCount,
                    s.shadow.shadowGpuSceneUploadedInstanceCount);
                MetricRowText("CPU Authored Command State",
                    cpuAuthoredClear ? "Clear" : "Active");
                ImGui::EndTable();
            }
        }

        void DrawSubmissionTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Submission");
            if (BeginMetricTable("SubmissionMetrics")) {
                MetricRowText("Route Mode", ToString(s.submission.routeMode));
                MetricRowText("Mainline",
                    s.submission.gpuDrivenMainRouteActive ? "on" : "off");
                MetricRow("Scene Scanned / Submitted / Culled / Hidden", "%d / %d / %d / %d",
                    s.submission.scannedModelCount,
                    s.submission.submittedModelCount,
                    s.submission.culledModelCount,
                    s.submission.hiddenModelCount);
                MetricRow("Surface Instances / Cluster Instances", "%u / %u",
                    s.scene.surfaceInstanceCount,
                    s.scene.clusteredGeometrySurfaceInstanceCount);
                MetricRow("GPU Records Source / Routed / Unsupported", "%u / %u / %u",
                    s.gpuRegistry.sourceRecordCount,
                    s.gpuRegistry.forwardRoutedRecordCount,
                    s.gpuRegistry.unsupportedForwardRecordCount);
                MetricRow("GPU Resident Opaque / Cluster Candidate / Instances", "%u / %u / %u",
                    s.gpuRegistry.forwardOpaqueResidentRecordCount,
                    s.gpuRegistry.forwardOpaqueClusterCandidateRecordCount,
                    s.gpuRegistry.forwardOpaqueGpuSceneStats.instanceCount);
                MetricRow("GPU Resource Instances / Missing / Cluster Ranges", "%u / %u / %u",
                    s.gpuRegistry.forwardOpaqueGpuSceneStats.resourceBackedInstanceCount,
                    s.gpuRegistry.forwardOpaqueGpuSceneStats.missingResourceHandleInstanceCount,
                    s.gpuRegistry.forwardOpaqueGpuSceneStats.clusterSurfaceRangeInstanceCount);
                MetricRow("Static Batch Reordered Opaque / Depth / Shadow", "%u / %u / %u",
                    s.gpuRegistry.forwardOpaqueBatchStats.reorderedRecordCount,
                    s.gpuRegistry.forwardDepthAwareBatchStats.reorderedRecordCount,
                    s.gpuRegistry.shadowBatchStats.reorderedRecordCount);
                MetricRow("Opaque Batch PSO / Material Runs", "%u -> %u / %u -> %u",
                    s.gpuRegistry.forwardOpaqueBatchStats.rawPsoRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.sortedPsoRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.rawMaterialRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.sortedMaterialRunCount);
                MetricRow("Opaque Resource Runs Mesh / Material / Cluster", "%u -> %u / %u -> %u / %u -> %u",
                    s.gpuRegistry.forwardOpaqueBatchStats.rawMeshResourceRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.sortedMeshResourceRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.rawMaterialResourceRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.sortedMaterialResourceRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.rawClusterResourceRunCount,
                    s.gpuRegistry.forwardOpaqueBatchStats.sortedClusterResourceRunCount);
                MetricRow("Surface ExecuteIndirect Opaque / DepthAware / Transparent", "%zu / %zu / %zu",
                    s.mesh.surfaceIndirectOpaqueCommandCount,
                    s.mesh.surfaceIndirectDepthAwareCommandCount,
                    s.mesh.surfaceIndirectTransparentCommandCount);
                MetricRow("Surface Indirect Uploaded / Filtered / Overflow", "%zu / %zu / %zu",
                    s.mesh.surfaceIndirectUploadedCommandCount,
                    s.mesh.surfaceIndirectFilteredCommandCount,
                    s.mesh.surfaceIndirectOverflowCommandCount);
                MetricRow("Surface Indirect Batches / Commands / Saved / Max", "%zu / %zu / %zu / %zu",
                    s.mesh.surfaceIndirectBatchSubmitCount,
                    s.mesh.surfaceIndirectBatchedCommandCount,
                    s.mesh.surfaceIndirectSavedSubmitCount,
                    s.mesh.surfaceIndirectMaxBatchCommandCount);
                ImGui::EndTable();
            }
        }

        void DrawClusterResourceTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Cluster Resources");
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

        void DrawSsaoAndOverlayTable(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("SSAO / Overlay");
            if (BeginMetricTable("SsaoOverlayMetrics")) {
                MetricRow("SSAO Mode / Valid / Half", "%s / %s / %s",
                    RENDER3D::SCREENSPACE::ToString(s.ssao.mode),
                    s.ssao.valid ? "yes" : "no",
                    s.ssao.halfResolution ? "yes" : "no");
                MetricRow("SSAO AO Size / Samples / Blur", "%u x %u / %u / %u",
                    s.ssao.internalWidth,
                    s.ssao.internalHeight,
                    s.ssao.sampleCount,
                    s.ssao.blurIterations);
                MetricRow("SSAO CPU Geometry / Main / Blur / Total", "%.3f / %.3f / %.3f / %.3f ms",
                    s.ssao.geometryAuxCpuMs,
                    s.ssao.mainCpuMs,
                    s.ssao.blurCpuMs,
                    s.ssao.totalCpuMs);
                MetricRow("Shadow Draws / AlphaMask / Enabled", "%zu / %zu / %s",
                    s.shadow.totalPrimitiveCasterDrawCount,
                    s.shadow.alphaMaskCasterDrawCount,
                    s.shadow.enabled ? "yes" : "no");
                MetricRow("Debug Lines / XRay / LightProbe", "%zu / %zu / %u",
                    s.debugOverlay.expandedLineCount,
                    s.debugOverlay.xrayLineCount,
                    s.debugOverlay.lightProbeGizmoDrawnPointCount);
                ImGui::EndTable();
            }
        }

        void DrawReadiness(const RuntimePerformanceSnapshot& s) {
            ImGui::SeparatorText("Readiness");
            if (ImGui::BeginTable("ReadinessTable", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                TextStatus("GPU Scene", s.mesh.surfaceGpuSceneSrvValid && s.mesh.surfaceGpuSceneBufferReady);
                TextStatus("Indirect Args", s.mesh.surfaceIndirectArgumentBufferReady && s.mesh.surfaceIndirectCommandSignatureReady);
                ImGui::TableNextColumn();
                TextStatus("Cluster Cull", s.mesh.clusterGpuCullReady && s.mesh.clusterGpuCullDrawArgsReady);
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
        DrawStrictGpuDrivenTable(snapshot);
        DrawGpuTimingTable(snapshot);
        DrawClusterRuntimeTable(snapshot);
        DrawSubmissionTable(snapshot);
        DrawClusterResourceTable(snapshot);
        DrawSsaoAndOverlayTable(snapshot);
#endif
    }

} // namespace HIKARI
