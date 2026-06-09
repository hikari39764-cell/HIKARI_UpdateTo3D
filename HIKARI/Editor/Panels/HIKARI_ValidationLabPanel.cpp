#include "HIKARI_ValidationLabPanel.h"

#include "Render3D/Cluster/HIKARI_ClusteredCpuPreviewRenderer.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
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

            ImGui::TextDisabled("SurfaceDrawPacket is the main forward surface route. Opaque/mask and transparent packets now have separate plans.");

            const bool instancePacketMatch = sceneStats.surfaceInstanceCount == packetStats.packetCount;
            const float validPacketRatio = SafeRatio(packetStats.validPacketCount, packetStats.packetCount);
            const float forwardRatio = SafeRatio(packetStats.forwardCandidateCount, packetStats.packetCount);
            const float shadowRatio = SafeRatio(packetStats.shadowCandidateCount, packetStats.packetCount);
            const float reorderedRatio = SafeRatio(packetStats.reorderedPacketCount, packetStats.sortEligiblePacketCount);

            ImGui::SeparatorText("Contract");
            ImGui::Text("Instance / Packet Count: %u / %u (%s)",
                sceneStats.surfaceInstanceCount,
                packetStats.packetCount,
                instancePacketMatch ? "Match" : "Mismatch");
            ImGui::Text("Valid Packet Ratio: %.1f%%", validPacketRatio * 100.0f);
            ImGui::Text("Forward / Shadow Candidate Ratio: %.1f%% / %.1f%%",
                forwardRatio * 100.0f,
                shadowRatio * 100.0f);
            ImGui::Text("Resource Key Invalid / Sort Breaks: %u / %u",
                packetStats.invalidResourceKeyCount,
                packetStats.sortOrderBreakCount);

            ImGui::SeparatorText("Packet Sort View");
            ImGui::Text("Eligible / Sorted / Reordered: %u / %u / %u (%.1f%%)",
                packetStats.sortEligiblePacketCount,
                packetStats.sortedPacketCount,
                packetStats.reorderedPacketCount,
                reorderedRatio * 100.0f);
            ImGui::Text("Transparent Resource-Sort Excluded: %u", packetStats.transparentResourceSortExcludedCount);
            ImGui::Text("Raw / Sorted Sort Breaks: %u / %u",
                packetStats.sortOrderBreakCount,
                packetStats.sortedSortOrderBreakCount);
            ImGui::Text("Runs Pass / PSO: %u -> %u / %u -> %u",
                packetStats.rawPassRunCount,
                packetStats.sortedPassRunCount,
                packetStats.rawPsoRunCount,
                packetStats.sortedPsoRunCount);
            ImGui::Text("Runs Material / Texture: %u -> %u / %u -> %u",
                packetStats.rawMaterialRunCount,
                packetStats.sortedMaterialRunCount,
                packetStats.rawTextureSetRunCount,
                packetStats.sortedTextureSetRunCount);
            ImGui::Text("Runs Geometry: %u -> %u",
                packetStats.rawGeometryRunCount,
                packetStats.sortedGeometryRunCount);

            ImGui::SeparatorText("SurfacePacket Main Route");
            const RenderSubmissionDebugStats& renderSubmissionStats =
                RenderSubmissionSystem::GetDebugStats();
            ImGui::Text("SurfacePacket Main Route Active: %s",
                renderSubmissionStats.surfacePacketMainRouteActive ? "true" : "false");
            ImGui::TextDisabled("SurfacePacket is locked as the normal surface route. RuntimeSpecial keeps only unsupported cases.");
            ImGui::Text("SurfacePacket Forward Bypass Objects: %d",
                renderSubmissionStats.surfacePacketForwardSkipCount);
            ImGui::Text("SurfacePacket Shadow Bypass Objects: %d",
                renderSubmissionStats.surfacePacketShadowSkipCount);
            ImGui::Text("RuntimeSpecial Models Forward / Shadow / Total: %d / %d / %d",
                renderSubmissionStats.runtimeSpecialForwardModelCount,
                renderSubmissionStats.runtimeSpecialShadowModelCount,
                renderSubmissionStats.runtimeSpecialModelCount);
            ImGui::Text("Source / Sorted Packets: %u / %u",
                planStats.sourcePacketCount,
                planStats.sortedPacketCount);
            ImGui::Text("Objects Candidate / Full / RuntimeSpecial / Main Bypass: %u / %u / %u / %u",
                planStats.candidateObjectCount,
                planStats.fullCoverageObjectCount,
                planStats.runtimeSpecialObjectCount,
                planStats.mainForwardBypassObjectCount);
            ImGui::Text("Objects Partial: %u",
                planStats.partialCoverageObjectCount);
            ImGui::Text("Packets Candidate / Submitted / Culled / Handled: %u / %u / %u / %u",
                planStats.candidatePacketCount,
                planStats.submittedForwardPacketCount,
                planStats.culledPacketCount,
                planStats.handledForwardPacketCount);
            ImGui::Text("Submitted Opaque / Transparent Packets: %u / %u",
                planStats.submittedForwardOpaquePacketCount,
                planStats.submittedForwardTransparentPacketCount);
            ImGui::Text("Submitted Commands / Single / Max Length: %u / %u / %u",
                planStats.submittedCommandCount,
                planStats.submittedSinglePacketCommandCount,
                planStats.submittedMaxCommandPacketCount);
            ImGui::Text("Opaque Commands / Single / Max Length: %u / %u / %u",
                planStats.submittedOpaqueCommandCount,
                planStats.submittedOpaqueSinglePacketCommandCount,
                planStats.submittedOpaqueMaxCommandPacketCount);
            ImGui::Text("Transparent Commands / Single / Max Length: %u / %u / %u",
                planStats.submittedTransparentCommandCount,
                planStats.submittedTransparentSinglePacketCommandCount,
                planStats.submittedTransparentMaxCommandPacketCount);
            ImGui::Text("GPU Scene Instances / Opaque / Transparent / Max Command: %u / %u / %u / %u",
                planStats.submittedGpuSceneInstanceCount,
                planStats.submittedOpaqueGpuSceneInstanceCount,
                planStats.submittedTransparentGpuSceneInstanceCount,
                planStats.submittedMaxGpuSceneCommandInstanceCount);
            ImGui::Text("Transparent DepthSort Candidate / Sorted / Reordered / Fallback: %u / %u / %u / %u",
                planStats.transparentDepthSortCandidateCount,
                planStats.transparentDepthSortedPacketCount,
                planStats.transparentDepthReorderedPacketCount,
                planStats.transparentDepthSortFallbackPacketCount);
            ImGui::Text("Skipped RuntimeAnim / Debug / Skinned / TransparentReject / MaskReject: %u / %u / %u / %u / %u",
                planStats.skippedRuntimeAnimationPacketCount,
                planStats.skippedSpecialDebugPacketCount,
                planStats.skippedSkinnedPacketCount,
                planStats.skippedTransparentPacketCount,
                planStats.skippedAlphaMaskedPacketCount);
            ImGui::Text("Skipped No Forward / Invalid / Invalid Key / Legacy Shader: %u / %u / %u / %u",
                planStats.skippedNoForwardPacketCount,
                planStats.skippedInvalidPacketCount,
                planStats.skippedInvalidResourceKeyCount,
                planStats.skippedLegacyShaderPacketCount);
            ImGui::Text("Skipped DepthAware / Invalid Primitive / Partial Coverage: %u / %u / %u",
                planStats.skippedDepthAwarePacketCount,
                planStats.skippedInvalidPrimitiveCount,
                planStats.skippedPartialCoveragePacketCount);

            ImGui::SeparatorText("Route Buckets");
            DrawSurfaceRouteBucketStats("Forward",
                planStats.forwardRouteBuckets);
            DrawSurfaceRouteBucketStats("Shadow",
                planStats.shadowRouteBuckets);

            ImGui::SeparatorText("SurfacePacket Shadow Plan");
            ImGui::Text("Shadow Objects Candidate / Full / RuntimeSpecial / Main Bypass: %u / %u / %u / %u",
                planStats.shadowCandidateObjectCount,
                planStats.shadowFullCoverageObjectCount,
                planStats.shadowRuntimeSpecialObjectCount,
                planStats.mainShadowBypassObjectCount);
            ImGui::Text("Shadow Objects Partial: %u",
                planStats.shadowPartialCoverageObjectCount);
            ImGui::Text("Shadow Packets Candidate / Planned / Handled: %u / %u / %u",
                planStats.shadowCandidatePacketCount,
                planStats.plannedShadowPacketCount,
                planStats.handledShadowPacketCount);
            ImGui::Text("Shadow Commands / Single / Max Length: %u / %u / %u",
                planStats.shadowCommandCount,
                planStats.shadowSinglePacketCommandCount,
                planStats.shadowMaxCommandPacketCount);
            ImGui::Text("Shadow GPU Scene Instances / Max Command: %u / %u",
                planStats.shadowGpuSceneInstanceCount,
                planStats.shadowMaxGpuSceneCommandInstanceCount);
            ImGui::Text("Shadow Drawn / Skipped / Runtime Commands: %zu / %zu / %zu",
                shadowStats.shadowPacketCasterDrawCount,
                shadowStats.shadowPacketSkippedCount,
                shadowStats.shadowPacketCommandCount);
            ImGui::Text("Shadow DrawCalls / Instanced / Instanced Casters: %zu / %zu / %zu",
                shadowStats.shadowPacketDrawCallCount,
                shadowStats.shadowPacketInstancedDrawCount,
                shadowStats.shadowPacketInstancedCasterCount);
            ImGui::Text("Shadow Max Instance Count: %zu",
                shadowStats.shadowPacketMaxInstanceCount);
            ImGui::Text("Shadow Skipped RuntimeAnim / Debug / Skinned / Transparent: %u / %u / %u / %u",
                planStats.shadowSkippedRuntimeAnimationPacketCount,
                planStats.shadowSkippedSpecialDebugPacketCount,
                planStats.shadowSkippedSkinnedPacketCount,
                planStats.shadowSkippedTransparentPacketCount);
            ImGui::Text("Shadow Skipped No Shadow / Invalid / Invalid Key / Primitive / Partial: %u / %u / %u / %u / %u",
                planStats.shadowSkippedNoShadowPacketCount,
                planStats.shadowSkippedInvalidPacketCount,
                planStats.shadowSkippedInvalidResourceKeyCount,
                planStats.shadowSkippedInvalidPrimitiveCount,
                planStats.shadowSkippedPartialCoveragePacketCount);

            ImGui::SeparatorText("Mesh Binding Cache");
            ImGui::Text("Root Signature Bind / Skip: %zu / %zu",
                meshStats.rootSignatureBindCount,
                meshStats.rootSignatureSkipCount);
            ImGui::Text("Frame Resource Bind / Skip: %zu / %zu",
                meshStats.frameResourceBindCount,
                meshStats.frameResourceSkipCount);
            ImGui::Text("Object Resource Bind / Skip: %zu / %zu",
                meshStats.objectResourceBindCount,
                meshStats.objectResourceSkipCount);
            ImGui::Text("Legacy ObjectCB Writes (RuntimeSpecial Routes): %zu", meshStats.legacyObjectCbWriteCount);
            ImGui::Text("ObjectData Writes: %zu", meshStats.objectDataWriteCount);
            ImGui::Text("ObjectData Buffer Bind / Skip: %zu / %zu",
                meshStats.objectDataBufferBindCount,
                meshStats.objectDataBufferSkipCount);
            ImGui::Text("ObjectData Index Bind / Skip: %zu / %zu",
                meshStats.objectIndexBindCount,
                meshStats.objectIndexSkipCount);
            ImGui::Text("MaterialData Writes / Cached: %zu / %zu",
                meshStats.materialDataWriteCount,
                meshStats.materialDataCachedCount);
            ImGui::Text("MaterialData Hit / Miss / Overflow: %zu / %zu / %zu",
                meshStats.materialDataCacheHitCount,
                meshStats.materialDataCacheMissCount,
                meshStats.materialDataOverflowCount);
            ImGui::Text("MaterialData Buffer Bind / Skip: %zu / %zu",
                meshStats.materialDataBufferBindCount,
                meshStats.materialDataBufferSkipCount);
            ImGui::Text("MaterialData Index Bind / Skip: %zu / %zu",
                meshStats.materialIndexBindCount,
                meshStats.materialIndexSkipCount);
            ImGui::Text("Texture Pool Slots Resolved / Invalid: %zu / %zu",
                meshStats.materialTexturePoolResolvedSlotCount,
                meshStats.materialTexturePoolInvalidSlotCount);
            ImGui::Text("Texture Pool Unique SRVs / Slots: %zu / %zu",
                meshStats.materialTexturePoolUniqueDescriptorCount,
                meshStats.materialTexturePoolSlotCount);
            ImGui::Text("Descriptor Table Bind / Skip: %zu / %zu",
                meshStats.descriptorTableBindCount,
                meshStats.descriptorTableSkipCount);
            ImGui::Text("Pipeline State Bind / Skip: %zu / %zu",
                meshStats.pipelineStateBindCount,
                meshStats.pipelineStateSkipCount);

            ImGui::SeparatorText("Surface Packet Executor");
            ImGui::Text("Packets / Skipped: %zu / %zu",
                meshStats.surfacePacketExecutorPacketCount,
                meshStats.surfacePacketExecutorSkippedPacketCount);
            ImGui::Text("Geometry / Forward Draws: %zu / %zu",
                meshStats.surfacePacketExecutorGeometryDrawCount,
                meshStats.surfacePacketExecutorForwardDrawCount);
            ImGui::Text("Forward Opaque / Transparent Draws: %zu / %zu",
                meshStats.surfacePacketExecutorOpaqueDrawCount,
                meshStats.surfacePacketExecutorTransparentDrawCount);
            const size_t instancedSavedDraws =
                meshStats.surfacePacketExecutorInstancedPacketCount >= meshStats.surfacePacketExecutorInstancedDrawCount
                    ? meshStats.surfacePacketExecutorInstancedPacketCount - meshStats.surfacePacketExecutorInstancedDrawCount
                    : 0;
            ImGui::Text("Instanced Draws / Packets / Saved: %zu / %zu / %zu",
                meshStats.surfacePacketExecutorInstancedDrawCount,
                meshStats.surfacePacketExecutorInstancedPacketCount,
                instancedSavedDraws);
            ImGui::Text("Max Instance Count: %zu",
                meshStats.surfacePacketExecutorMaxInstanceCount);
            ImGui::Text("Commands / Single / Max Length: %zu / %zu / %zu",
                meshStats.surfacePacketExecutorCommandCount,
                meshStats.surfacePacketExecutorSinglePacketCommandCount,
                meshStats.surfacePacketExecutorMaxCommandPacketCount);
            ImGui::Text("Opaque / Transparent Runtime Commands: %zu / %zu",
                meshStats.surfacePacketExecutorOpaqueCommandCount,
                meshStats.surfacePacketExecutorTransparentCommandCount);

            ImGui::SeparatorText("Scene Surface Instances");
            ImGui::Text("Total: %u", sceneStats.surfaceInstanceCount);
            ImGui::Text("Visible / Hidden: %u / %u",
                sceneStats.visibleSurfaceInstanceCount,
                sceneStats.hiddenSurfaceInstanceCount);
            ImGui::Text("Static / Dynamic Surfaces: %u / %u",
                sceneStats.staticSurfaceInstanceCount,
                sceneStats.dynamicSurfaceInstanceCount);
            ImGui::Text("Static Geometry / Skinned Geometry: %u / %u",
                sceneStats.staticGeometrySurfaceInstanceCount,
                sceneStats.skinnedSurfaceInstanceCount);
            ImGui::Text("Invalid / Missing Matrix / Invalid Bounds: %u / %u / %u",
                sceneStats.invalidSurfaceInstanceCount,
                sceneStats.missingSurfaceMatrixCount,
                sceneStats.invalidSurfaceBoundsCount);

            ImGui::SeparatorText("Surface Draw Packets");
            ImGui::Text("Packets: %u", packetStats.packetCount);
            ImGui::Text("Valid / Invalid: %u / %u",
                packetStats.validPacketCount,
                packetStats.invalidPacketCount);
            ImGui::Text("Visible / Hidden: %u / %u",
                packetStats.visiblePacketCount,
                packetStats.hiddenPacketCount);
            ImGui::Text("Static / Dynamic: %u / %u",
                packetStats.staticPacketCount,
                packetStats.dynamicPacketCount);
            ImGui::Text("Static Geometry / Skinned: %u / %u",
                packetStats.staticGeometryPacketCount,
                packetStats.skinnedPacketCount);
            ImGui::Text("Material Asset / Override: %u / %u",
                packetStats.materialAssetPacketCount,
                packetStats.materialOverridePacketCount);
            ImGui::Text("Opaque / Mask / Transparent: %u / %u / %u",
                packetStats.opaquePacketCount,
                packetStats.alphaMaskedPacketCount,
                packetStats.transparentPacketCount);
            ImGui::Text("Buckets Model / Geometry / Material: %u / %u / %u",
                packetStats.modelBucketCount,
                packetStats.geometryBucketCount,
                packetStats.materialBucketCount);
            ImGui::Text("Buckets Texture / Shader / PSO: %u / %u / %u",
                packetStats.textureSetBucketCount,
                packetStats.shaderBucketCount,
                packetStats.psoBucketCount);
            ImGui::Text("Forward / Shadow Candidates: %u / %u",
                packetStats.forwardCandidateCount,
                packetStats.shadowCandidateCount);
            ImGui::Text("Invalid Source / Model / Geometry: %u / %u / %u",
                packetStats.invalidSourceCount,
                packetStats.invalidModelCount,
                packetStats.unsupportedGeometryCount);
            ImGui::Text("Missing Matrix / Invalid Bounds / Invalid Primitive: %u / %u / %u",
                packetStats.missingDrawMatrixCount,
                packetStats.invalidBoundsCount,
                packetStats.invalidPrimitiveIndexCount);
        }

        void DrawClusterValidationSection(EditorContext& context) {
            if (!ImGui::CollapsingHeader("Clustered Geometry Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            ImGui::TextDisabled("Temporary HCMESH preview path. CPU reference remains isolated from the normal SurfacePacket route.");
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
        ImGui::TextDisabled("Temporary validation and migration controls. Delete sections when their phase passes.");
        DrawSurfacePacketValidationSection();
        DrawClusterValidationSection(context);
#else
        (void)context;
#endif
    }

} // namespace HIKARI
