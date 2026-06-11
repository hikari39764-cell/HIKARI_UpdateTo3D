#include "HIKARI_ValidationLabPanel.h"

#include "Render3D/Cluster/HIKARI_ClusteredCpuPreviewRenderer.h"
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

        void DrawSurfaceRouteBucketStats(
            const char* label,
            const RENDER3D::RUNTIME::SurfaceDrawRouteBucketStats& stats) {

            ImGui::Text("%s Main / MainOpaque / MainMask / MainTransparent: %u / %u / %u / %u",
                label,
                stats.mainRoutePacketCount,
                stats.mainOpaquePacketCount,
                stats.mainAlphaMaskPacketCount,
                stats.mainTransparentPacketCount);
            ImGui::Text("%s MaskReject / TransparentReject / DepthAware: %u / %u / %u",
                label,
                stats.alphaMaskPacketCount,
                stats.transparentPacketCount,
                stats.depthAwarePacketCount);
            ImGui::Text("%s RuntimeSpecial / Skinned / Legacy / Invalid / NoPass: %u / %u / %u / %u / %u",
                label,
                stats.runtimeSpecialPacketCount,
                stats.skinnedPacketCount,
                stats.legacyShaderPacketCount,
                stats.invalidPacketCount,
                stats.noPassPacketCount);
        }

        bool DrawRenderSubmissionRouteModeCombo(RenderSubmissionRouteMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Submission Route", ToString(mode))) {
                const RenderSubmissionRouteMode modes[] = {
                    RenderSubmissionRouteMode::SurfacePacketMainline,
                    RenderSubmissionRouteMode::ForceLegacy,
                };
                for (RenderSubmissionRouteMode candidate : modes) {
                    if (ImGui::Selectable(ToString(candidate), mode == candidate)) {
                        mode = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool DrawClusteredRenderModeCombo(RENDER3D::CLUSTER::ClusteredRenderMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Render Mode", RENDER3D::CLUSTER::ToString(mode))) {
                const RENDER3D::CLUSTER::ClusteredRenderMode modes[] = {
                    RENDER3D::CLUSTER::ClusteredRenderMode::Off,
                    RENDER3D::CLUSTER::ClusteredRenderMode::SelectedPreview,
                    RENDER3D::CLUSTER::ClusteredRenderMode::CpuReference,
                };
                for (RENDER3D::CLUSTER::ClusteredRenderMode candidate : modes) {
                    if (ImGui::Selectable(
                            RENDER3D::CLUSTER::ToString(candidate),
                            mode == candidate)) {
                        mode = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool DrawClusterDebugViewCombo(RENDER3D::CLUSTER::ClusterDebugViewMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Debug View", RENDER3D::CLUSTER::ToString(mode))) {
                const RENDER3D::CLUSTER::ClusterDebugViewMode modes[] = {
                    RENDER3D::CLUSTER::ClusterDebugViewMode::Off,
                    RENDER3D::CLUSTER::ClusterDebugViewMode::SelectedObjectSummary,
                    RENDER3D::CLUSTER::ClusterDebugViewMode::SelectedSurfaceBounds,
                    RENDER3D::CLUSTER::ClusterDebugViewMode::FirstNClusterBounds,
                    RENDER3D::CLUSTER::ClusterDebugViewMode::ClusterPageBounds,
                    RENDER3D::CLUSTER::ClusterDebugViewMode::ClusterColorMesh,
                    RENDER3D::CLUSTER::ClusterDebugViewMode::PageColorMesh,
                    RENDER3D::CLUSTER::ClusterDebugViewMode::SurfaceColorMesh,
                };
                for (RENDER3D::CLUSTER::ClusterDebugViewMode candidate : modes) {
                    if (ImGui::Selectable(
                            RENDER3D::CLUSTER::ToString(candidate),
                            mode == candidate)) {
                        mode = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        void DrawClusterDebugOptions(RENDER3D::CLUSTER::ClusterDebugOptions& options) {
            DrawClusterDebugViewCombo(options.mode);

            if (options.mode == RENDER3D::CLUSTER::ClusterDebugViewMode::SelectedSurfaceBounds) {
                int surfaceIndex = static_cast<int>(options.selectedSurfaceIndex);
                if (ImGui::DragInt("Surface Index", &surfaceIndex, 1.0f, 0, 4096)) {
                    options.selectedSurfaceIndex = static_cast<uint32_t>((std::max)(0, surfaceIndex));
                }
            }
            if (options.mode == RENDER3D::CLUSTER::ClusterDebugViewMode::FirstNClusterBounds) {
                int clusterLimit = static_cast<int>(options.firstClusterLimit);
                if (ImGui::DragInt("Cluster Limit", &clusterLimit, 1.0f, 1, 256)) {
                    options.firstClusterLimit = static_cast<uint32_t>((std::clamp)(clusterLimit, 1, 256));
                }
            }
            if (options.mode == RENDER3D::CLUSTER::ClusterDebugViewMode::ClusterPageBounds) {
                int pageLimit = static_cast<int>(options.pageLimit);
                if (ImGui::DragInt("Page Limit", &pageLimit, 1.0f, 1, 128)) {
                    options.pageLimit = static_cast<uint32_t>((std::clamp)(pageLimit, 1, 128));
                }
            }
        }

        void DrawSurfacePacketValidationSection() {
            const RENDER3D::RUNTIME::SceneRenderCache::Stats& sceneStats =
                RenderSubmissionSystem::GetSceneRenderCacheStats();
            const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder::Stats& packetStats =
                RenderSubmissionSystem::GetSurfaceDrawPacketStats();
            const RENDER3D::RUNTIME::SurfaceDrawPacketPlanStats& planStats =
                RenderSubmissionSystem::GetSurfaceDrawPacketPlanStats();
            const MESHRENDERER::MeshRendererDebugStats& meshStats =
                MESHRENDERER::GetDebugStats();
            const SHADOW::ShadowMapDebugStats& shadowStats =
                SHADOW::GetDebugStats();

            if (!ImGui::CollapsingHeader("Surface / DrawPacket Contract Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            const bool instancePacketMatch = sceneStats.surfaceInstanceCount == packetStats.packetCount;
            const float validPacketRatio = SafeRatio(packetStats.validPacketCount, packetStats.packetCount);
            const float forwardRatio = SafeRatio(packetStats.forwardCandidateCount, packetStats.packetCount);
            const float shadowRatio = SafeRatio(packetStats.shadowCandidateCount, packetStats.packetCount);
            const RenderSubmissionDebugStats& renderSubmissionStats =
                RenderSubmissionSystem::GetDebugStats();
            const bool gpuSceneReady =
                meshStats.surfaceGpuSceneSrvValid &&
                meshStats.surfaceGpuSceneBufferReady &&
                meshStats.surfaceGpuSceneOverflowInstanceCount == 0;
            const bool indirectReady =
                meshStats.surfaceIndirectArgumentBufferReady &&
                meshStats.surfaceIndirectCommandSignatureReady &&
                meshStats.surfaceIndirectOverflowCommandCount == 0;
            const bool shadowIndirectReady =
                shadowStats.shadowIndirectArgumentBufferReady &&
                shadowStats.shadowIndirectCommandSignatureReady &&
                shadowStats.shadowIndirectOverflowCommandCount == 0;
            const bool hasOpaqueWork = planStats.submittedOpaqueCommandCount > 0;
            const bool opaqueIndirectComplete =
                meshStats.surfaceIndirectOpaqueCommandCount == meshStats.surfacePacketExecutorOpaqueDrawCount &&
                meshStats.surfaceIndirectFallbackCommandCount == 0 &&
                meshStats.surfaceGpuSceneMaterialPatchFailCount == 0;
            const char* opaqueMainlineStatus = !hasOpaqueWork
                ? "Idle"
                : (renderSubmissionStats.surfacePacketMainRouteActive &&
                    gpuSceneReady &&
                    indirectReady &&
                    opaqueIndirectComplete ? "Ready" : "Watch");
            const size_t instancedSavedDraws =
                meshStats.surfacePacketExecutorInstancedPacketCount >= meshStats.surfacePacketExecutorInstancedDrawCount
                    ? meshStats.surfacePacketExecutorInstancedPacketCount - meshStats.surfacePacketExecutorInstancedDrawCount
                    : 0;

            ImGui::SeparatorText("Contract");
            RenderSubmissionRouteMode routeMode = RenderSubmissionSystem::GetRouteMode();
            if (DrawRenderSubmissionRouteModeCombo(routeMode)) {
                RenderSubmissionSystem::SetRouteMode(routeMode);
            }
            ImGui::Text("Instance / Packet Count: %u / %u (%s)",
                sceneStats.surfaceInstanceCount,
                packetStats.packetCount,
                instancePacketMatch ? "Match" : "Mismatch");
            ImGui::Text("Valid / Static / Skinned Packets: %u / %u / %u",
                packetStats.validPacketCount,
                packetStats.staticGeometryPacketCount,
                packetStats.skinnedPacketCount);
            ImGui::Text("Cluster Surface / Resource Packets: %u / %u",
                sceneStats.clusteredGeometrySurfaceInstanceCount,
                packetStats.clusterGeometryResourcePacketCount);
            ImGui::Text("Geometry Backend Triangle / Cluster: %u / %u",
                packetStats.triangleGeometryBackendPacketCount,
                packetStats.clusterGeometryBackendPacketCount);
            ImGui::Text("Resource Identity / Pool Handles / Missing Pool: %u / %u / %u",
                packetStats.resourceIdentityPacketCount,
                packetStats.resourcePoolHandlePacketCount,
                packetStats.resourcePoolMissingPacketCount);
            ImGui::Text("Valid Packet Ratio / Forward / Shadow: %.1f%% / %.1f%% / %.1f%%",
                validPacketRatio * 100.0f,
                forwardRatio * 100.0f,
                shadowRatio * 100.0f);

            ImGui::SeparatorText("Static Forward Mainline");
            ImGui::Text("Opaque Mainline Status: %s", opaqueMainlineStatus);
            ImGui::Text("Route Mode / Mainline Active / ForceLegacy: %s / %s / %s",
                ToString(renderSubmissionStats.routeMode),
                renderSubmissionStats.surfacePacketMainRouteActive ? "true" : "false",
                renderSubmissionStats.surfacePacketForceLegacyActive ? "true" : "false");
            ImGui::Text("Forward Bypass / RuntimeSpecial Fwd / Total: %d / %d / %d",
                renderSubmissionStats.surfacePacketForwardSkipCount,
                renderSubmissionStats.runtimeSpecialForwardModelCount,
                renderSubmissionStats.runtimeSpecialModelCount);
            ImGui::Text("Source / Sorted / Candidate Packets: %u / %u / %u",
                planStats.sourcePacketCount,
                planStats.sortedPacketCount,
                planStats.candidatePacketCount);
            ImGui::Text("Objects Candidate / Full / RuntimeSpecial / Bypass / Partial: %u / %u / %u / %u / %u",
                planStats.candidateObjectCount,
                planStats.fullCoverageObjectCount,
                planStats.runtimeSpecialObjectCount,
                planStats.mainForwardBypassObjectCount,
                planStats.partialCoverageObjectCount);
            ImGui::Text("Packets Candidate / Submitted / Culled / Handled: %u / %u / %u / %u",
                planStats.candidatePacketCount,
                planStats.submittedForwardPacketCount,
                planStats.culledPacketCount,
                planStats.handledForwardPacketCount);
            ImGui::Text("Submitted Opaque / DepthAware / Transparent Packets: %u / %u / %u",
                planStats.submittedForwardOpaquePacketCount,
                planStats.submittedForwardDepthAwarePacketCount,
                planStats.submittedForwardTransparentPacketCount);
            ImGui::Text("Plan Opaque Commands / Merged / Saved / Max: %u / %u / %u / %u",
                planStats.submittedOpaqueCommandCount,
                planStats.submittedOpaqueMergedCommandCount,
                planStats.submittedOpaqueSavedCommandCount,
                planStats.submittedOpaqueMaxCommandPacketCount);
            ImGui::Text("Plan DepthAware Commands / Merged / Saved / Max: %u / %u / %u / %u",
                planStats.submittedDepthAwareCommandCount,
                planStats.submittedDepthAwareMergedCommandCount,
                planStats.submittedDepthAwareSavedCommandCount,
                planStats.submittedDepthAwareMaxCommandPacketCount);
            ImGui::Text("Plan Transparent Commands / Merged / Saved / Max: %u / %u / %u / %u",
                planStats.submittedTransparentCommandCount,
                planStats.submittedTransparentMergedCommandCount,
                planStats.submittedTransparentSavedCommandCount,
                planStats.submittedTransparentMaxCommandPacketCount);
            ImGui::Text("Plan Indirect Ready / Missing Args: %u / %u",
                planStats.submittedIndirectReadyCommandCount,
                planStats.submittedMissingDrawArgsCommandCount);

            ImGui::SeparatorText("GPU Driven Submission");
            ImGui::Text("GPU Scene Ready / Uploaded / Overflow: %s / %zu / %zu",
                gpuSceneReady ? "Ready" : "Missing",
                meshStats.surfaceGpuSceneUploadedInstanceCount,
                meshStats.surfaceGpuSceneOverflowInstanceCount);
            ImGui::Text("Indirect Buffer Ready / Uploaded / Filtered / Binding / Direct / Missing / Overflow: %s / %zu / %zu / %zu / %zu / %zu / %zu",
                indirectReady ? "Ready" : "Missing",
                meshStats.surfaceIndirectUploadedCommandCount,
                meshStats.surfaceIndirectFilteredCommandCount,
                meshStats.surfaceIndirectDrawBindingPatchCount,
                meshStats.surfaceIndirectCpuDirectCommandCount,
                meshStats.surfaceIndirectMissingDrawArgsCommandCount,
                meshStats.surfaceIndirectOverflowCommandCount);
            ImGui::Text("GPU Scene Instances / Opaque / DepthAware / Transparent / Max Command: %u / %u / %u / %u / %u",
                planStats.submittedGpuSceneInstanceCount,
                planStats.submittedOpaqueGpuSceneInstanceCount,
                planStats.submittedDepthAwareGpuSceneInstanceCount,
                planStats.submittedTransparentGpuSceneInstanceCount,
                planStats.submittedMaxGpuSceneCommandInstanceCount);
            ImGui::Text("GPU Scene Resource Instances / Missing: %u / %u",
                planStats.submittedGpuSceneResourceInstanceCount,
                planStats.submittedGpuSceneMissingResourceInstanceCount);
            ImGui::Text("GPU Scene Cluster Resource / SRV / Range / Missing Range: %u / %u / %u / %u",
                planStats.submittedGpuSceneClusterResourceInstanceCount,
                planStats.submittedGpuSceneClusterShaderVisibleInstanceCount,
                planStats.submittedGpuSceneClusterSurfaceRangeInstanceCount,
                planStats.submittedGpuSceneClusterMissingSurfaceRangeInstanceCount);
            ImGui::Text("Cluster GPU Cull Ready / GPUScene Source / Candidate / Submitted Inst / Overflow: %s / %zu / %zu / %zu / %zu",
                meshStats.clusterGpuCullReady ? "Ready" : "Missing",
                meshStats.clusterGpuCullSourceInstanceCount,
                meshStats.clusterGpuCullCandidateInstanceCount,
                meshStats.clusterGpuCullSubmittedInstanceCount,
                meshStats.clusterGpuCullOverflowInstanceCount);
            ImGui::Text("Cluster GPU Source SingleSided / DoubleSided Inst: %zu / %zu",
                meshStats.clusterGpuCullSourceSingleSidedInstanceCount,
                meshStats.clusterGpuCullSourceDoubleSidedInstanceCount);
            ImGui::Text("Cluster GPU Page Tasks CPU Seeds / GPU Expanded / Overflow: %zu / %zu / %zu",
                meshStats.clusterGpuCullSourcePageTaskCount,
                meshStats.clusterGpuCullGpuPageTaskCount,
                meshStats.clusterGpuCullGpuPageTaskOverflowCount);
            ImGui::Text("Cluster GPU Cull Debug Counters: %s",
                meshStats.clusterGpuCullDebugCountersEnabled ? "on" : "off");
            ImGui::Text("Cluster GPU Visible Runs / Input Culled / Clusters / DrawArgs / Overflow: %s / %zu / %zu / %zu / %zu / %zu",
                meshStats.clusterGpuCullCounterReadbackValid ? "Valid" :
                    (meshStats.clusterGpuCullCounterReadbackReady ? "Waiting" : "Missing"),
                meshStats.clusterGpuCullGpuVisibleRangeCount,
                meshStats.clusterGpuCullGpuInputFrustumCulledCount,
                meshStats.clusterGpuCullGpuVisibleClusterCount,
                meshStats.clusterGpuCullGpuDrawCommandCount,
                meshStats.clusterGpuCullGpuDrawCommandOverflowCount +
                    meshStats.clusterGpuCullGpuOverflowCount);
            ImGui::Text("Cluster GPU Page Tested / Frustum Culled: %zu / %zu",
                meshStats.clusterGpuCullGpuPageTestedCount,
                meshStats.clusterGpuCullGpuPageFrustumCulledCount);
            ImGui::Text("Cluster GPU Cluster Tested / Frustum / Cone Culled: %zu / %zu / %zu",
                meshStats.clusterGpuCullGpuClusterTestedCount,
                meshStats.clusterGpuCullGpuClusterFrustumCulledCount,
                meshStats.clusterGpuCullGpuClusterConeCulledCount);
            ImGui::Text("Cluster GPU Cone Tested / DoubleSided Skip Cone: %zu / %zu",
                meshStats.clusterGpuCullGpuClusterConeTestedCount,
                meshStats.clusterGpuCullGpuDoubleSidedClusterCount);
            ImGui::Text("Cluster GPU DrawArgs BackFace / DoubleSided / Overflow: %zu / %zu / %zu / %zu",
                meshStats.clusterGpuCullGpuBackFaceDrawCommandCount,
                meshStats.clusterGpuCullGpuDoubleSidedDrawCommandCount,
                meshStats.clusterGpuCullGpuBackFaceDrawCommandOverflowCount,
                meshStats.clusterGpuCullGpuDoubleSidedDrawCommandOverflowCount);
            ImGui::Text("Cluster Draw Eligible / Reject Mainline / Backend / Transparent: %zu / %zu / %zu / %zu",
                meshStats.clusterDrawEligibleCommandCount,
                meshStats.clusterDrawRejectMainlineCommandCount,
                meshStats.clusterDrawRejectBackendCommandCount,
                meshStats.clusterDrawRejectTransparentCommandCount);
            ImGui::Text("Cluster Draw Reject Range / Resource / Flags / MaterialFx / Material / Context: %zu / %zu / %zu / %zu / %zu / %zu",
                meshStats.clusterDrawRejectRangeCommandCount,
                meshStats.clusterDrawRejectInstanceResourceCommandCount,
                meshStats.clusterDrawRejectInstanceFlagCommandCount,
                meshStats.clusterDrawRejectMaterialFxCommandCount,
                meshStats.clusterDrawRejectMaterialPatchCommandCount,
                meshStats.clusterDrawRejectContextCommandCount);
            ImGui::Text("Cluster GPU Cull Dispatch / Groups / Capacity: %zu / %zu / %zu",
                meshStats.clusterGpuCullDispatchCount,
                meshStats.clusterGpuCullWorkgroupCount,
                meshStats.clusterGpuCullVisibleRangeCapacity);
            ImGui::Text("Cluster Draw Args Ready / Signature / GPU PageTasks / Capacity: %s / %s / %zu / %zu",
                meshStats.clusterGpuCullDrawArgsReady ? "Ready" : "Missing",
                meshStats.clusterGpuCullCommandSignatureReady ? "Ready" : "Missing",
                meshStats.clusterGpuCullGpuPageTaskCount,
                meshStats.clusterGpuCullDrawArgumentCapacity);
            ImGui::Text("Cluster Draw Executor Pipeline / Forward / Geometry / Args / Signature: %s / %s / %s / %s / %s",
                meshStats.clusterDrawPipelineReady ? "Ready" : "Pending",
                meshStats.clusterDrawForwardPipelineReady ? "Ready" : "Pending",
                meshStats.clusterDrawGeometryBufferPipelineReady ? "Ready" : "Pending",
                meshStats.clusterDrawArgumentBufferReady ? "Ready" : "Missing",
                meshStats.clusterDrawCommandSignatureReady ? "Ready" : "Missing");
            ImGui::Text("Cluster Mainline Ready / Forward / GeometryAux / Seeds / OverflowBlock: %s / %s / %s / %s / %s",
                meshStats.clusterMainlineReady ? "Ready" : "Blocked",
                meshStats.clusterMainlineForwardReady ? "Ready" : "Blocked",
                meshStats.clusterMainlineGeometryBufferReady ? "Ready" : "Blocked",
                meshStats.clusterMainlineHasDrawSeeds ? "yes" : "no",
                meshStats.clusterMainlineOverflowBlocked ? "yes" : "no");
            ImGui::Text("Opaque Ownership Cluster / GeometryAux / Legacy Commands: %zu / %zu / %zu",
                meshStats.clusterMainlineOwnedCommandCount,
                meshStats.clusterMainlineGeometryAuxCommandCount,
                meshStats.clusterMainlineLegacyCommandCount);
            ImGui::Text("Opaque Ownership Cluster / GeometryAux / Legacy Packets: %zu / %zu / %zu",
                meshStats.clusterMainlineOwnedPacketCount,
                meshStats.clusterMainlineGeometryAuxPacketCount,
                meshStats.clusterMainlineLegacyPacketCount);
            ImGui::Text("Cluster Draw Requested / Submitted / Skipped / Calls / EmptyBuckets: %zu / %zu / %zu / %zu / %zu",
                meshStats.clusterDrawRequestedCount,
                meshStats.clusterDrawSubmittedCount,
                meshStats.clusterDrawSkippedCount,
                meshStats.clusterDrawSubmitCallCount,
                meshStats.clusterDrawSkippedBucketCount);
            ImGui::Text("Cluster Draw Forward / Geometry Submitted / Calls: %zu / %zu / %zu / %zu",
                meshStats.clusterDrawForwardSubmittedCount,
                meshStats.clusterDrawGeometryBufferSubmittedCount,
                meshStats.clusterDrawForwardSubmitCallCount,
                meshStats.clusterDrawGeometryBufferSubmitCallCount);
            ImGui::Text("Cluster Draw Bucket Calls BackFace / DoubleSided: %zu / %zu",
                meshStats.clusterDrawBackFaceSubmitCallCount,
                meshStats.clusterDrawDoubleSidedSubmitCallCount);
            ImGui::Text("Cluster Mainline Legacy Bypass Commands / Packets: %zu / %zu",
                meshStats.clusterDrawBypassedLegacyCommandCount,
                meshStats.clusterDrawBypassedLegacyPacketCount);
            ImGui::Text("ExecuteIndirect Opaque Commands / Packets: %zu / %zu",
                meshStats.surfaceIndirectOpaqueCommandCount,
                meshStats.surfaceIndirectOpaquePacketCount);
            ImGui::Text("ExecuteIndirect DepthAware Commands / Packets: %zu / %zu",
                meshStats.surfaceIndirectDepthAwareCommandCount,
                meshStats.surfaceIndirectDepthAwarePacketCount);
            ImGui::Text("ExecuteIndirect Transparent Commands / Packets: %zu / %zu",
                meshStats.surfaceIndirectTransparentCommandCount,
                meshStats.surfaceIndirectTransparentPacketCount);
            ImGui::Text("ExecuteIndirect Batches / Commands / Saved / Max: %zu / %zu / %zu / %zu",
                meshStats.surfaceIndirectBatchSubmitCount,
                meshStats.surfaceIndirectBatchedCommandCount,
                meshStats.surfaceIndirectSavedSubmitCount,
                meshStats.surfaceIndirectMaxBatchCommandCount);
            ImGui::Text("Instanced Draws / Packets / Saved / Max: %zu / %zu / %zu / %zu",
                meshStats.surfacePacketExecutorInstancedDrawCount,
                meshStats.surfacePacketExecutorInstancedPacketCount,
                instancedSavedDraws,
                meshStats.surfacePacketExecutorMaxInstanceCount);
            ImGui::Text("GPU Scene Draw Commands / Packets / Fallbacks / PatchFail: %zu / %zu / %zu / %zu",
                meshStats.surfacePacketExecutorGpuSceneDrawCount,
                meshStats.surfacePacketExecutorGpuScenePacketCount,
                meshStats.surfacePacketExecutorGpuSceneFallbackCount,
                meshStats.surfaceGpuSceneMaterialPatchFailCount);

            ImGui::SeparatorText("Unsupported Handoff");
            DrawSurfaceRouteBucketStats("Forward Route", planStats.forwardRouteBuckets);
            ImGui::Text("Skipped RuntimeAnim / Debug / Skinned / TransparentReject / MaskReject: %u / %u / %u / %u / %u",
                planStats.skippedRuntimeAnimationPacketCount,
                planStats.skippedSpecialDebugPacketCount,
                planStats.skippedSkinnedPacketCount,
                planStats.skippedTransparentPacketCount,
                planStats.skippedAlphaMaskedPacketCount);
            ImGui::Text("Skipped No Forward / Invalid / Invalid Key / Legacy Shader / DepthAware: %u / %u / %u / %u / %u",
                planStats.skippedNoForwardPacketCount,
                planStats.skippedInvalidPacketCount,
                planStats.skippedInvalidResourceKeyCount,
                planStats.skippedLegacyShaderPacketCount,
                planStats.skippedDepthAwarePacketCount);
            ImGui::Text("Transparent DepthSort Candidate / Sorted / Reordered / Fallback: %u / %u / %u / %u",
                planStats.transparentDepthSortCandidateCount,
                planStats.transparentDepthSortedPacketCount,
                planStats.transparentDepthReorderedPacketCount,
                planStats.transparentDepthSortFallbackPacketCount);

            ImGui::SeparatorText("Shadow Plan");
            ImGui::Text("Shadow Objects Candidate / Full / RuntimeSpecial / Main Bypass: %u / %u / %u / %u",
                planStats.shadowCandidateObjectCount,
                planStats.shadowFullCoverageObjectCount,
                planStats.shadowRuntimeSpecialObjectCount,
                planStats.mainShadowBypassObjectCount);
            ImGui::Text("Shadow Packets Candidate / Planned / Handled: %u / %u / %u",
                planStats.shadowCandidatePacketCount,
                planStats.plannedShadowPacketCount,
                planStats.handledShadowPacketCount);
            ImGui::Text("Shadow Commands / Merged / Saved / Max: %u / %u / %u / %u",
                planStats.shadowCommandCount,
                planStats.shadowMergedCommandCount,
                planStats.shadowSavedCommandCount,
                planStats.shadowMaxCommandPacketCount);
            ImGui::Text("Shadow GPU Scene Ready / Uploaded / Overflow: %s / %zu / %zu",
                shadowStats.shadowGpuSceneSrvValid && shadowStats.shadowGpuSceneBufferReady ? "Ready" : "Missing",
                shadowStats.shadowGpuSceneUploadedInstanceCount,
                shadowStats.shadowGpuSceneOverflowInstanceCount);
            ImGui::Text("Shadow GPU Scene Resource Instances / Missing: %u / %u",
                planStats.shadowGpuSceneResourceInstanceCount,
                planStats.shadowGpuSceneMissingResourceInstanceCount);
            ImGui::Text("Shadow GPU Scene Cluster Resource / SRV / Range / Missing Range: %u / %u / %u / %u",
                planStats.shadowGpuSceneClusterResourceInstanceCount,
                planStats.shadowGpuSceneClusterShaderVisibleInstanceCount,
                planStats.shadowGpuSceneClusterSurfaceRangeInstanceCount,
                planStats.shadowGpuSceneClusterMissingSurfaceRangeInstanceCount);
            ImGui::Text("Shadow Indirect Ready / Uploaded / Binding / Direct / Missing / Overflow: %s / %zu / %zu / %zu / %zu / %zu",
                shadowIndirectReady ? "Ready" : "Missing",
                shadowStats.shadowIndirectUploadedCommandCount,
                shadowStats.shadowIndirectDrawBindingPatchCount,
                shadowStats.shadowIndirectCpuDirectCommandCount,
                shadowStats.shadowIndirectMissingDrawArgsCommandCount,
                shadowStats.shadowIndirectOverflowCommandCount);
            ImGui::Text("Shadow ExecuteIndirect Batches / Commands / Packets / Saved / Max: %zu / %zu / %zu / %zu / %zu",
                shadowStats.shadowIndirectBatchSubmitCount,
                shadowStats.shadowIndirectExecutedCommandCount,
                shadowStats.shadowIndirectExecutedPacketCount,
                shadowStats.shadowIndirectSavedSubmitCount,
                shadowStats.shadowIndirectMaxBatchCommandCount);
            ImGui::Text("Shadow DrawCommands / Instanced / Drawn / Skipped / IndirectFallback: %zu / %zu / %zu / %zu / %zu",
                shadowStats.shadowPacketDrawCallCount,
                shadowStats.shadowPacketInstancedDrawCount,
                shadowStats.shadowPacketCasterDrawCount,
                shadowStats.shadowPacketSkippedCount,
                shadowStats.shadowIndirectFallbackCommandCount);
            ImGui::Text("Shadow Skipped RuntimeAnim / Debug / Skinned / Transparent: %u / %u / %u / %u",
                planStats.shadowSkippedRuntimeAnimationPacketCount,
                planStats.shadowSkippedSpecialDebugPacketCount,
                planStats.shadowSkippedSkinnedPacketCount,
                planStats.shadowSkippedTransparentPacketCount);
        }

        void DrawClusterValidationSection(EditorContext& context) {
            if (!ImGui::CollapsingHeader("Clustered Geometry Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            ImGui::TextDisabled("HCMESH cache and CPU reference are kept as cluster authoring tools; runtime submission uses SurfacePacket.");
            DrawClusteredRenderModeCombo(context.clusteredGeometry.renderMode);

            if (context.selection.selectedObject) {
                ImGui::Text("Selected: %s", context.selection.selectedObject->GetName().c_str());
            } else {
                ImGui::TextDisabled("Selected: <none>");
            }

            DrawClusterDebugOptions(context.clusteredGeometry.debugOptions);

            const RENDER3D::CLUSTER::ClusteredCpuPreviewStats& previewStats =
                RENDER3D::CLUSTER::GetClusteredCpuPreviewRenderer().GetStats();
            ImGui::SeparatorText("CPU Reference Frame");
            ImGui::Text("Mode: %s", RENDER3D::CLUSTER::ToString(previewStats.mode));
            ImGui::Text("Candidates: %u", previewStats.candidateObjectCount);
            ImGui::Text("Submitted Objects / Surfaces: %u / %u",
                previewStats.submittedObjectCount,
                previewStats.submittedSurfaceCount);
            ImGui::Text("Selected Preview Objects: %u", previewStats.selectedPreviewObjectCount);
            ImGui::Text("Fallback Objects / Surfaces: %u / %u",
                previewStats.fallbackObjectCount,
                previewStats.fallbackSurfaceCount);
            ImGui::Text("Transparent / Unsupported Surfaces: %u / %u",
                previewStats.transparentFallbackSurfaceCount,
                previewStats.unsupportedFallbackSurfaceCount);
            ImGui::Text("Cached / Rebuilt Preview Models: %u / %u",
                previewStats.cachedPreviewModelCount,
                previewStats.rebuiltPreviewModelCount);

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
            ImGui::Text("Surfaces / Clusters / Pages: %u / %u / %u",
                clusterStats.surfaceCount,
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
            ImGui::Text("GPU Surfaces / Clusters / Pages: %u / %u / %u",
                clusterResourceStats.surfaceCount,
                clusterResourceStats.clusterCount,
                clusterResourceStats.pageCount);
            ImGui::Text("GPU Surface Ranges: %u", clusterResourceStats.surfaceRangeCount);
            ImGui::Text("GPU Vertices / Indices / Bytes: %u / %u / %.2f MB",
                clusterResourceStats.vertexCount,
                clusterResourceStats.indexCount,
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
        DrawSurfacePacketValidationSection();
        DrawClusterValidationSection(context);
#else
        (void)context;
#endif
    }

} // namespace HIKARI
