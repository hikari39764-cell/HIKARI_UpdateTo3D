#include "HIKARI_ValidationLabPanel.h"

#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include <algorithm>
#include <cstdio>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        template <typename Numerator, typename Denominator>
        float SafeRatio(Numerator numerator, Denominator denominator) {
            const double safeDenominator = static_cast<double>(denominator);
            return safeDenominator > 0.0 ?
                static_cast<float>(static_cast<double>(numerator) / safeDenominator) :
                0.0f;
        }

        int ClampScore(float score) {
            if (score < 0.0f) {
                return 0;
            }
            if (score > 100.0f) {
                return 100;
            }
            return static_cast<int>(score + 0.5f);
        }

        int RatioPenalty(float ratio, int maxPenalty) {
            return ClampScore(ratio * static_cast<float>(maxPenalty));
        }

        const char* ScoreBandText(int score) {
            if (score >= 80) {
                return "Good";
            }
            if (score >= 55) {
                return "Watch";
            }
            return "Risk";
        }

        ImVec4 ScoreBandColor(int score) {
            if (score >= 80) {
                return ImVec4(0.28f, 0.82f, 0.45f, 1.0f);
            }
            if (score >= 55) {
                return ImVec4(0.95f, 0.72f, 0.25f, 1.0f);
            }
            return ImVec4(0.95f, 0.34f, 0.32f, 1.0f);
        }

        void DrawValidationScoreCard(const char* label, int score, const char* detail) {
            ImGui::PushID(label);
            const ImVec4 color = ScoreBandColor(score);
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::TextColored(color, "%d / 100 (%s)", score, ScoreBandText(score));

            char overlay[32]{};
            std::snprintf(overlay, sizeof(overlay), "%d", score);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
            ImGui::ProgressBar(static_cast<float>(score) / 100.0f, ImVec2(-1.0f, 0.0f), overlay);
            ImGui::PopStyleColor();
            ImGui::TextWrapped("%s", detail);
            ImGui::PopID();
        }

        void DrawGpuDrivenSubmissionValidationSection() {
            const RENDER3D::RUNTIME::SceneRenderCache::Stats& sceneStats =
                RenderSubmissionSystem::GetSceneRenderCacheStats();
            const RENDER3D::GPUDRIVEN::GpuSceneRegistryStats& registryStats =
                RenderSubmissionSystem::GetGpuSceneRegistryStats();
            const MESHRENDERER::MeshRendererDebugStats& meshStats =
                MESHRENDERER::GetDebugStats();
            const SHADOW::ShadowMapDebugStats& shadowStats =
                SHADOW::GetDebugStats();

            if (!ImGui::CollapsingHeader("GPU Driven Submission Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            const RenderSubmissionDebugStats& renderSubmissionStats =
                RenderSubmissionSystem::GetDebugStats();
            const bool gpuSceneReady =
                meshStats.surfaceGpuSceneSrvValid &&
                meshStats.surfaceGpuSceneBufferReady &&
                meshStats.surfaceGpuSceneOverflowInstanceCount == 0;
            const bool meshletReady =
                meshStats.meshletBackendPipelineReady &&
                meshStats.meshletBackendDispatchArgumentBufferReady &&
                meshStats.meshletBackendDispatchCommandSignatureReady;
            const bool clusterReady =
                meshStats.clusterGpuCullReady &&
                meshStats.clusterGpuCullDrawArgsReady &&
                meshStats.clusterGpuCullCommandSignatureReady;
            const bool hasMainlineWork =
                registryStats.forwardOpaqueResidentRecordCount > 0 ||
                meshStats.clusterGpuCullSubmittedInstanceCount > 0 ||
                meshStats.meshletBackendSubmittedDispatchCount > 0;
            const char* mainlineStatus = !hasMainlineWork ? "Idle" :
                (gpuSceneReady && clusterReady && meshletReady ? "Ready" : "Watch");

            ImGui::SeparatorText("Route");
            ImGui::Text("Route: %s", ToString(RenderSubmissionSystem::GetRouteMode()));
            ImGui::Text("Status / Mainline: %s / %s",
                mainlineStatus,
                renderSubmissionStats.gpuDrivenMainRouteActive ? "on" : "off");
            ImGui::Text("Scene Scan / Submit / Cull / Hidden: %d / %d / %d / %d",
                renderSubmissionStats.scannedModelCount,
                renderSubmissionStats.submittedModelCount,
                renderSubmissionStats.culledModelCount,
                renderSubmissionStats.hiddenModelCount);

            ImGui::SeparatorText("Scene Cache");
            ImGui::Text("Objects Visible / Hidden / Dirty: %u / %u / %u",
                sceneStats.visibleObjectCount,
                sceneStats.hiddenObjectCount,
                sceneStats.dirtyObjectCount);
            ImGui::Text("Surfaces Total / Visible / Dirty: %u / %u / %u",
                sceneStats.surfaceInstanceCount,
                sceneStats.visibleSurfaceInstanceCount,
                sceneStats.dirtySurfaceInstanceCount);
            ImGui::Text("Surfaces Static / Dynamic / Skinned / Clustered: %u / %u / %u / %u",
                sceneStats.staticSurfaceInstanceCount,
                sceneStats.dynamicSurfaceInstanceCount,
                sceneStats.skinnedSurfaceInstanceCount,
                sceneStats.clusteredGeometrySurfaceInstanceCount);

            ImGui::SeparatorText("GPU Registry");
            ImGui::Text("Source Records / Routed / Unsupported: %u / %u / %u",
                registryStats.sourceRecordCount,
                registryStats.forwardRoutedRecordCount,
                registryStats.unsupportedForwardRecordCount);
            ImGui::Text("Forward Opaque Resident / Cluster Candidate: %u / %u",
                registryStats.forwardOpaqueResidentRecordCount,
                registryStats.forwardOpaqueClusterCandidateRecordCount);
            ImGui::Text("GPU Scene Instances Opaque / DepthAware / Transparent: %u / %u / %u",
                registryStats.forwardOpaqueGpuSceneStats.instanceCount,
                registryStats.forwardDepthAwareGpuSceneStats.instanceCount,
                registryStats.forwardTransparentGpuSceneStats.instanceCount);
            ImGui::Text("Resource Instances / Missing: %u / %u",
                registryStats.forwardOpaqueGpuSceneStats.resourceBackedInstanceCount,
                registryStats.forwardOpaqueGpuSceneStats.missingResourceHandleInstanceCount);
            ImGui::Text("Cluster Resource / SRV / Range / Missing Range: %u / %u / %u / %u",
                registryStats.forwardOpaqueGpuSceneStats.clusterResourceInstanceCount,
                registryStats.forwardOpaqueGpuSceneStats.clusterShaderVisibleInstanceCount,
                registryStats.forwardOpaqueGpuSceneStats.clusterSurfaceRangeInstanceCount,
                registryStats.forwardOpaqueGpuSceneStats.clusterMissingSurfaceRangeInstanceCount);

            ImGui::SeparatorText("GPU Execution");
            ImGui::Text("GPU Scene Buffer Ready / Uploaded / Requested / Overflow: %s / %zu / %zu / %zu",
                gpuSceneReady ? "Ready" : "Missing",
                meshStats.surfaceGpuSceneUploadedInstanceCount,
                meshStats.surfaceGpuSceneRequestedInstanceCount,
                meshStats.surfaceGpuSceneOverflowInstanceCount);
            ImGui::Text("Worklist Passes / Cluster Passes / Instances / Cluster Instances: %zu / %zu / %zu / %zu",
                meshStats.gpuDrivenWorklistPassCount,
                meshStats.gpuDrivenWorklistClusterPassCount,
                meshStats.gpuDrivenWorklistSourceInstanceCount,
                meshStats.gpuDrivenWorklistClusterInstanceCount);
            ImGui::Text("Command Stream Passes / Ranges / GPU Cmd / Traditional VS/PS: %zu / %zu / %zu / %zu",
                meshStats.gpuDrivenCommandStreamPassCount,
                meshStats.gpuDrivenCommandStreamRangeCount,
                meshStats.gpuDrivenCommandStreamGpuCommandCount,
                meshStats.gpuDrivenCommandStreamTraditionalCommandCount);
            ImGui::Text("Cluster Cull Ready / Source / Candidate / Submitted / Overflow: %s / %zu / %zu / %zu / %zu",
                clusterReady ? "Ready" : "Missing",
                meshStats.clusterGpuCullSourceInstanceCount,
                meshStats.clusterGpuCullCandidateInstanceCount,
                meshStats.clusterGpuCullSubmittedInstanceCount,
                meshStats.clusterGpuCullOverflowInstanceCount);
            ImGui::Text("Cluster Visible Runs / Clusters / DrawArgs / Overflow: %zu / %zu / %zu / %zu",
                meshStats.clusterGpuCullGpuVisibleRangeCount,
                meshStats.clusterGpuCullGpuVisibleClusterCount,
                meshStats.clusterGpuCullGpuDrawCommandCount,
                meshStats.clusterGpuCullGpuDrawCommandOverflowCount +
                    meshStats.clusterGpuCullGpuOverflowCount);
            ImGui::Text("Cluster HZB Occlusion / Size / Page Culled / Cluster Culled: %s / %zux%zu / %zu / %zu",
                meshStats.clusterGpuCullHzbOcclusionEnabled ? "on" : "off",
                meshStats.clusterGpuCullHzbOcclusionWidth,
                meshStats.clusterGpuCullHzbOcclusionHeight,
                meshStats.clusterGpuCullGpuPageOcclusionCulledCount,
                meshStats.clusterGpuCullGpuClusterOcclusionCulledCount);
            ImGui::Text("Cluster HZB Reject Pass / AABB / Sphere / Accepted / Raw: %zu / %zu / %zu / %zu / %zu",
                meshStats.clusterGpuCullGpuHzbPassRejectedCount,
                meshStats.clusterGpuCullGpuHzbAabbRejectedCount,
                meshStats.clusterGpuCullGpuHzbSphereRejectedCount,
                meshStats.clusterGpuCullGpuHzbQueryAcceptedCount,
                meshStats.clusterGpuCullGpuHzbRawOccludedCount);
            ImGui::Text("Cluster HZB Temporal Ready / Capacity / Pending / Confirmed: %s / %zu / %zu / %zu",
                meshStats.clusterGpuCullOcclusionHistoryReady ? "yes" : "no",
                meshStats.clusterGpuCullOcclusionHistoryCapacity,
                meshStats.clusterGpuCullGpuHzbTemporalPendingCount,
                meshStats.clusterGpuCullGpuHzbTemporalConfirmedCount);
            ImGui::Text("GPU LOD Selected L0 / L1 / L2 / L3+: %zu / %zu / %zu / %zu",
                meshStats.clusterGpuCullGpuLod0SelectedCount,
                meshStats.clusterGpuCullGpuLod1SelectedCount,
                meshStats.clusterGpuCullGpuLod2SelectedCount,
                meshStats.clusterGpuCullGpuLod3PlusSelectedCount);
            ImGui::Text("Meshlet Backend Ready / SM6.5 / Tier / Fwd / GBuffer / PSO: %s / %s / %u / %s / %s / %zu/%zu",
                meshletReady ? "Ready" : "Missing",
                meshStats.meshletBackendShaderModel65Supported ? "yes" : "no",
                meshStats.meshletBackendMeshShaderTier,
                meshStats.meshletBackendForwardPipelineReady ? "Ready" : "Pending",
                meshStats.meshletBackendGeometryAuxPipelineReady ? "Ready" : "Pending",
                meshStats.meshletBackendPipelineCreateReadyCount,
                meshStats.meshletBackendPipelineCreateRequestCount);
            ImGui::Text("Meshlet Draw Requested / Submitted / Calls / EmptyBuckets: %zu / %zu / %zu / %zu",
                meshStats.meshletBackendRequestedDispatchCount,
                meshStats.meshletBackendSubmittedDispatchCount,
                meshStats.meshletBackendSubmitCallCount,
                meshStats.meshletBackendSkippedBucketCount);
            ImGui::Text("Meshlet Draw Forward / Geometry / BackFace / DoubleSided Calls: %zu / %zu / %zu / %zu",
                meshStats.meshletBackendForwardSubmittedDispatchCount,
                meshStats.meshletBackendGeometryAuxSubmittedDispatchCount,
                meshStats.meshletBackendBackFaceSubmitCallCount,
                meshStats.meshletBackendDoubleSidedSubmitCallCount);
            ImGui::Text("Traditional VS/PS ExecuteIndirect Opaque / DepthAware / Transparent: %zu / %zu / %zu",
                meshStats.traditionalCommandStreamOpaqueCommandCount,
                meshStats.traditionalCommandStreamDepthAwareCommandCount,
                meshStats.traditionalCommandStreamTransparentCommandCount);

            ImGui::SeparatorText("Shadow Baseline");
            ImGui::Text("Shadow GPU Scene Ready / Uploaded / Overflow: %s / %zu / %zu",
                shadowStats.shadowGpuSceneSrvValid && shadowStats.shadowGpuSceneBufferReady ? "Ready" : "Missing",
                shadowStats.shadowGpuSceneUploadedInstanceCount,
                shadowStats.shadowGpuSceneOverflowInstanceCount);
            ImGui::Text("Shadow DrawCommands / Instanced / Drawn / Skipped: %zu / %zu / %zu / %zu",
                shadowStats.shadowRecordDrawCallCount,
                shadowStats.shadowRecordInstancedDrawCount,
                shadowStats.shadowRecordCasterDrawCount,
                shadowStats.shadowRecordSkippedCount);
        }

        void DrawClusterValidationSection(EditorContext& context) {
            (void)context;
            if (!ImGui::CollapsingHeader("Clustered Geometry Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            const RENDER3D::CLUSTER::ClusteredGeometryManagerStats& clusterStats =
                RENDER3D::CLUSTER::GetClusteredGeometryManager().GetStats();
            ImGui::SeparatorText("Runtime HCMESH Cache");
            ImGui::Text("Requests / Hits / Misses: %u / %u / %u",
                clusterStats.requestCount,
                clusterStats.hitCount,
                clusterStats.missCount);
            ImGui::Text("Valid / Invalid: %u / %u",
                clusterStats.validAssetCount,
                clusterStats.invalidAssetCount);
            ImGui::Text("Surfaces / LOD Ranges / Clusters / Pages: %u / %u / %u / %u",
                clusterStats.surfaceCount,
                clusterStats.surfaceLodRangeCount,
                clusterStats.clusterCount,
                clusterStats.pageCount);
            ImGui::Text("Triangles / Vertices: %u / %u",
                clusterStats.totalTriangleCount,
                clusterStats.totalVertexCount);
            ImGui::Text("Max Vertices / Cluster: %u", clusterStats.maxVerticesPerCluster);
            ImGui::TextWrapped("Last Load: %s",
                RENDER3D::CLUSTER::GetClusteredGeometryManager().GetLastMessage().empty()
                    ? "<none>"
                    : RENDER3D::CLUSTER::GetClusteredGeometryManager().GetLastMessage().c_str());

            const RENDER3D::ClusterGeometryResourceSystemStats clusterResourceStats =
                RENDER3D::GetClusterGeometryResourceSystemStats();
            ImGui::SeparatorText("Cluster GPU Resources");
            ImGui::Text("Context / Ready Resources / Failed: %s / %u / %u",
                clusterResourceStats.initialized ? "Ready" : "Missing",
                clusterResourceStats.readyResourceCount,
                clusterResourceStats.failedCount);
            ImGui::Text("Requests / Hits / Misses / Loaded: %u / %u / %u / %u",
                clusterResourceStats.requestCount,
                clusterResourceStats.hitCount,
                clusterResourceStats.missCount,
                clusterResourceStats.loadedCount);
            ImGui::Text("Resources / Upgraded Virtual Handles: %u / %u",
                clusterResourceStats.resourceCount,
                clusterResourceStats.upgradedVirtualHandleCount);
            ImGui::Text("Shader SRV Ready / Missing / Allocation Failed: %u / %u / %u",
                clusterResourceStats.shaderVisibleResourceCount,
                clusterResourceStats.missingDescriptorCount,
                clusterResourceStats.descriptorAllocationFailedCount);
            ImGui::Text("GPU Surfaces / LOD Ranges / Clusters / Pages: %u / %u / %u / %u",
                clusterResourceStats.surfaceCount,
                clusterResourceStats.surfaceLodRangeCount,
                clusterResourceStats.clusterCount,
                clusterResourceStats.pageCount);
            ImGui::Text("GPU Surface Ranges: %u", clusterResourceStats.surfaceRangeCount);
            ImGui::Text("GPU Vertices / Indices / Primitives / Bytes: %u / %u / %u / %.2f MB",
                clusterResourceStats.vertexCount,
                clusterResourceStats.indexCount,
                clusterResourceStats.meshletPrimitiveCount,
                static_cast<double>(clusterResourceStats.gpuBufferBytes) / (1024.0 * 1024.0));

            const RENDER3D::RenderResourceDescriptorPoolStats descriptorStats =
                RENDER3D::GetRenderResourceDescriptorPoolStats();
            ImGui::Text("Resource Descriptor Pool: %s, used %u / %u, failed %u",
                descriptorStats.initialized ? "Ready" : "Missing",
                descriptorStats.used,
                descriptorStats.capacity,
                descriptorStats.failedAllocationCount);
        }
    }
#endif

    void ValidationLabPanel::Draw(EditorContext& context, bool& open) const {
#if defined(HIKARI_WITH_EDITOR)
        if (!open) {
            return;
        }
        if (!ImGui::Begin("Validation Lab", &open)) {
            ImGui::End();
            return;
        }

        DrawContents(context);

        ImGui::End();
#else
        (void)context;
        (void)open;
#endif
    }

    void ValidationLabPanel::DrawContents(EditorContext& context) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::TextDisabled("Renderer contract, GPU-driven submission, and cluster resource status.");
        DrawGpuDrivenSubmissionValidationSection();
        DrawClusterValidationSection(context);
#else
        (void)context;
#endif
    }

} // namespace HIKARI
