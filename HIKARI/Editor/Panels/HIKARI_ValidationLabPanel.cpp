#include "HIKARI_ValidationLabPanel.h"

#include "Render3D/Cluster/HIKARI_ClusteredCpuPreviewRenderer.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
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

        void DrawStaticDrawMigrationAudit(
            const RenderSubmissionDebugStats& renderSubmissionStats,
            const RENDER3D::RUNTIME::StaticDrawRecordCache::Stats& cacheStats,
            const RENDER3D::RUNTIME::StaticDrawRecordSubmitStats& submitStats,
            bool enabled,
            bool cachedForward,
            bool cachedShadow,
            bool bypassOldModelRenderer) {
            const float objectCoverageRatio = cacheStats.staticObjectCount > 0u ?
                SafeRatio(cacheStats.fullCoverageObjectCount, cacheStats.staticObjectCount) :
                1.0f;
            const float forwardRecordCoverageRatio = cacheStats.expectedForwardSubmeshCount > 0u ?
                SafeRatio(cacheStats.validForwardRecordCount, cacheStats.expectedForwardSubmeshCount) :
                1.0f;

            int coverageScore = ClampScore(
                (objectCoverageRatio * 0.45f + forwardRecordCoverageRatio * 0.55f) * 100.0f);
            coverageScore -= RatioPenalty(
                SafeRatio(
                    cacheStats.noCoverageObjectCount + cacheStats.invalidCoverageObjectCount,
                    cacheStats.staticObjectCount),
                45);
            coverageScore -= RatioPenalty(
                SafeRatio(cacheStats.partialCoverageObjectCount, cacheStats.staticObjectCount),
                15);
            coverageScore = ClampScore(static_cast<float>(coverageScore));

            const float forwardSkipRatio = SafeRatio(
                renderSubmissionStats.staticCachedForwardSkipCount,
                renderSubmissionStats.staticCachedCandidateCount);
            const float shadowSkipRatio = SafeRatio(
                renderSubmissionStats.staticCachedShadowSkipCount,
                renderSubmissionStats.staticCachedCandidateCount);
            const float bypassRatio = SafeRatio(
                renderSubmissionStats.staticCachedBypassOldModelRendererCount,
                renderSubmissionStats.staticCachedCandidateCount);
            const float fallbackRatio = SafeRatio(
                renderSubmissionStats.staticCachedFallbackCount,
                renderSubmissionStats.staticCachedCandidateCount);

            int takeoverScore = 85;
            if (!enabled) {
                takeoverScore = 35;
            } else if (renderSubmissionStats.staticCachedCandidateCount <= 0) {
                takeoverScore = cacheStats.staticObjectCount > 0u ? 50 : 85;
            } else {
                takeoverScore = ClampScore(
                    (bypassRatio * 0.35f +
                        forwardSkipRatio * 0.25f +
                        shadowSkipRatio * 0.20f +
                        (1.0f - fallbackRatio) * 0.20f) *
                    100.0f);
                if (!cachedForward) {
                    takeoverScore -= 12;
                }
                if (!cachedShadow) {
                    takeoverScore -= 10;
                }
                if (!bypassOldModelRenderer) {
                    takeoverScore -= 12;
                }
            }
            takeoverScore = ClampScore(static_cast<float>(takeoverScore));

            int safetyScore = 100;
            safetyScore -= RatioPenalty(
                SafeRatio(cacheStats.invalidCoverageObjectCount, cacheStats.staticObjectCount),
                35);
            safetyScore -= RatioPenalty(
                SafeRatio(cacheStats.noCoverageObjectCount, cacheStats.staticObjectCount),
                20);
            safetyScore -= cacheStats.invalidRecordBoundsCount > 0u ? 18 : 0;
            safetyScore -= cacheStats.missingDrawMatrixCount > 0u ? 18 : 0;
            safetyScore -= cacheStats.invalidPrimitiveIndexCount > 0u ? 18 : 0;
            safetyScore -= submitStats.skippedInvalidRecordCount > 0u ? 12 : 0;
            safetyScore -= submitStats.skippedUnsupportedRecordCount > 0u ? 8 : 0;
            safetyScore = ClampScore(static_cast<float>(safetyScore));

            ImGui::SeparatorText("Migration Audit");
            if (ImGui::BeginTable("StaticDrawMigrationAudit", 3, ImGuiTableFlags_SizingStretchSame)) {
                char detail[192]{};
                ImGui::TableNextColumn();
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Objects %.1f%%, forward records %.1f%%",
                    objectCoverageRatio * 100.0f,
                    forwardRecordCoverageRatio * 100.0f);
                DrawValidationScoreCard("Coverage", coverageScore, detail);

                ImGui::TableNextColumn();
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Candidates %d, bypass %.1f%%, fallback %.1f%%",
                    renderSubmissionStats.staticCachedCandidateCount,
                    bypassRatio * 100.0f,
                    fallbackRatio * 100.0f);
                DrawValidationScoreCard("Takeover", takeoverScore, detail);

                ImGui::TableNextColumn();
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Invalid bounds %u, missing matrix %u, invalid primitive %u",
                    cacheStats.invalidRecordBoundsCount,
                    cacheStats.missingDrawMatrixCount,
                    cacheStats.invalidPrimitiveIndexCount);
                DrawValidationScoreCard("Safety", safetyScore, detail);

                ImGui::EndTable();
            }
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

        void DrawStaticDrawValidationSection() {
            const RenderSubmissionDebugStats& renderSubmissionStats =
                RenderSubmissionSystem::GetDebugStats();
            const RENDER3D::RUNTIME::StaticDrawRecordCache::Stats& cacheStats =
                RenderSubmissionSystem::GetStaticDrawRecordCacheStats();
            const RENDER3D::RUNTIME::StaticDrawRecordSubmitStats& submitStats =
                RenderSubmissionSystem::GetStaticDrawRecordSubmitStats();

            if (!ImGui::CollapsingHeader("Static Draw Takeover Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            ImGui::TextDisabled("Temporary compatibility path. Remove after SurfaceInstance/DrawPacket replaces it.");

            bool enabled = RenderSubmissionSystem::IsUseStaticDrawRecordCacheEnabled();
            bool cachedForward = RenderSubmissionSystem::IsUseCachedStaticForwardEnabled();
            bool cachedShadow = RenderSubmissionSystem::IsUseCachedStaticShadowEnabled();
            bool bypassOldModelRenderer = RenderSubmissionSystem::IsBypassOldStaticModelRendererEnabled();

            DrawStaticDrawMigrationAudit(
                renderSubmissionStats,
                cacheStats,
                submitStats,
                enabled,
                cachedForward,
                cachedShadow,
                bypassOldModelRenderer);

            ImGui::SeparatorText("Controls");
            if (ImGui::Checkbox("Use Static Draw Record Cache", &enabled)) {
                RenderSubmissionSystem::SetUseStaticDrawRecordCache(enabled);
            }
            if (ImGui::Checkbox("Submit Cached Forward", &cachedForward)) {
                RenderSubmissionSystem::SetUseCachedStaticForward(cachedForward);
            }
            if (ImGui::Checkbox("Submit Cached Shadow", &cachedShadow)) {
                RenderSubmissionSystem::SetUseCachedStaticShadow(cachedShadow);
            }
            if (ImGui::Checkbox("Bypass Legacy Static Submit", &bypassOldModelRenderer)) {
                RenderSubmissionSystem::SetBypassOldStaticModelRenderer(bypassOldModelRenderer);
            }

            ImGui::SeparatorText("Cache");
            ImGui::Text("Static / Cached Objects: %u / %u", cacheStats.staticObjectCount, cacheStats.cachedObjectCount);
            ImGui::Text("Cached Records: %u", cacheStats.cachedRecordCount);
            ImGui::Text("Valid Forward Records: %u / %u",
                cacheStats.validForwardRecordCount,
                cacheStats.expectedForwardSubmeshCount);
            ImGui::Text("Coverage Full / Partial / None / Invalid: %u / %u / %u / %u",
                cacheStats.fullCoverageObjectCount,
                cacheStats.partialCoverageObjectCount,
                cacheStats.noCoverageObjectCount,
                cacheStats.invalidCoverageObjectCount);
            ImGui::Text("Skipped Dynamic / Animated / Debug: %u / %u / %u",
                cacheStats.skippedDynamicObjectCount,
                cacheStats.skippedAnimatedObjectCount,
                cacheStats.skippedDebugModeObjectCount);

            ImGui::SeparatorText("Submit");
            ImGui::Text("Candidates: %d", renderSubmissionStats.staticCachedCandidateCount);
            ImGui::Text("Skip Legacy Forward / Shadow Objects: %d / %d",
                renderSubmissionStats.staticCachedForwardSkipCount,
                renderSubmissionStats.staticCachedShadowSkipCount);
            ImGui::Text("Bypass Legacy Objects: %d",
                renderSubmissionStats.staticCachedBypassOldModelRendererCount);
            ImGui::Text("Submitted Forward / Shadow Records: %d / %d",
                renderSubmissionStats.staticCachedSubmittedForwardRecordCount,
                renderSubmissionStats.staticCachedSubmittedShadowRecordCount);
            ImGui::Text("Culled Forward Records: %d", renderSubmissionStats.staticCachedCulledRecordCount);
            ImGui::Text("Shadow Cull Skipped Records: %u", submitStats.shadowCullSkippedCount);
            ImGui::Text("Skipped Invalid / Unsupported Records: %u / %u",
                submitStats.skippedInvalidRecordCount,
                submitStats.skippedUnsupportedRecordCount);
        }

        void DrawSurfacePacketValidationSection() {
            const RENDER3D::RUNTIME::SceneRenderCache::Stats& sceneStats =
                RenderSubmissionSystem::GetSceneRenderCacheStats();
            const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder::Stats& packetStats =
                RenderSubmissionSystem::GetSurfaceDrawPacketStats();
            const RENDER3D::RUNTIME::SurfaceDrawPacketSubmitStats& submitStats =
                RenderSubmissionSystem::GetSurfaceDrawPacketSubmitStats();
            const MESHRENDERER::MeshRendererDebugStats& meshStats =
                MESHRENDERER::GetDebugStats();

            if (!ImGui::CollapsingHeader("Surface / DrawPacket Contract Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            ImGui::TextDisabled("Temporary SurfaceInstance and DrawPacket checks. Remove after the pooled resource path takes over.");

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
            ImGui::Text("Transparent Sort Excluded: %u", packetStats.transparentSortExcludedCount);
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

            ImGui::SeparatorText("SurfacePacket Forward Submit");
            bool sortedForwardPreview = RenderSubmissionSystem::IsUseSortedSurfaceForwardPreviewEnabled();
            bool skipLegacyForward = RenderSubmissionSystem::IsSkipOldStaticForwardWhenSortedSurfaceEnabled();
            if (ImGui::Checkbox("Submit SurfacePacket Forward", &sortedForwardPreview)) {
                RenderSubmissionSystem::SetUseSortedSurfaceForwardPreview(sortedForwardPreview);
            }
            if (ImGui::Checkbox("Skip Legacy Static Forward", &skipLegacyForward)) {
                RenderSubmissionSystem::SetSkipOldStaticForwardWhenSortedSurface(skipLegacyForward);
            }
            ImGui::TextDisabled("Active only when both toggles are enabled. Transparent, mask, dynamic and skinned packets stay on legacy path.");
            ImGui::Text("Source / Sorted Packets: %u / %u",
                submitStats.sourcePacketCount,
                submitStats.sortedPacketCount);
            ImGui::Text("Objects Candidate / Full / Fallback / Skip Old: %u / %u / %u / %u",
                submitStats.candidateObjectCount,
                submitStats.fullCoverageObjectCount,
                submitStats.fallbackObjectCount,
                submitStats.skipOldForwardObjectCount);
            ImGui::Text("Objects Partial / Primitive Handoff: %u / %u",
                submitStats.partialCoverageObjectCount,
                submitStats.handledPrimitiveObjectCount);
            ImGui::Text("Packets Candidate / Submitted / Culled / Handled: %u / %u / %u / %u",
                submitStats.candidatePacketCount,
                submitStats.submittedForwardPacketCount,
                submitStats.culledPacketCount,
                submitStats.handledForwardPacketCount);
            ImGui::Text("Partial Packets / Handled Primitives: %u / %u",
                submitStats.partialTakeoverPacketCount,
                submitStats.handledPrimitiveCount);
            ImGui::Text("Submitted Runs / Single / Max Length: %u / %u / %u",
                submitStats.submittedRunCount,
                submitStats.submittedSinglePacketRunCount,
                submitStats.submittedMaxRunPacketCount);
            ImGui::Text("Skipped Dynamic / Skinned / Transparent / Mask: %u / %u / %u / %u",
                submitStats.skippedDynamicPacketCount,
                submitStats.skippedSkinnedPacketCount,
                submitStats.skippedTransparentPacketCount,
                submitStats.skippedAlphaMaskedPacketCount);
            ImGui::Text("Skipped No Forward / Invalid / Invalid Key: %u / %u / %u",
                submitStats.skippedNoForwardPacketCount,
                submitStats.skippedInvalidPacketCount,
                submitStats.skippedInvalidResourceKeyCount);
            ImGui::Text("Skipped Invalid Primitive / Partial Coverage: %u / %u",
                submitStats.skippedInvalidPrimitiveCount,
                submitStats.skippedPartialCoveragePacketCount);

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
            ImGui::Text("Legacy ObjectCB Writes: %zu", meshStats.legacyObjectCbWriteCount);
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
            ImGui::Text("Runs / Single / Max Length: %zu / %zu / %zu",
                meshStats.surfacePacketExecutorRunCount,
                meshStats.surfacePacketExecutorSinglePacketRunCount,
                meshStats.surfacePacketExecutorMaxRunPacketCount);

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

            ImGui::TextDisabled("Temporary HCMESH preview path. Keep legacy rendering as visual fallback.");
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
        DrawStaticDrawValidationSection();
        DrawClusterValidationSection(context);
#else
        (void)context;
#endif
    }

} // namespace HIKARI
