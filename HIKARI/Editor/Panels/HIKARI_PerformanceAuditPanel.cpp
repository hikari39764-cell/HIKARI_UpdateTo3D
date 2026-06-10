#include "HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include <cstdio>

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
    namespace {

        struct AuditSnapshot {
            int cpuSubmissionHeadroom = 0;
            int passHeadroom = 0;
            int performanceHeadroom = 0;

            int sceneSubmissionHeadroom = 0;
            int modelRendererHeadroom = 0;
            int forwardHeadroom = 0;
            int shadowHeadroom = 0;
            int ssaoHeadroom = 0;
            int debugOverlayHeadroom = 0;

            const char* dominantRiskName = "None";
            int dominantRisk = 0;

            float fpsRaw = 0.0f;
            int scannedModelCount = 0;
            int hiddenModelCount = 0;
            int culledModelCount = 0;
            int missingBoundsCount = 0;
            int skinnedCullSkippedCount = 0;
            int submittedModelCount = 0;
            int structuredNodeCount = 0;
            int matrixBuildCount = 0;
            size_t forwardDrawCount = 0;
            size_t shadowDrawCount = 0;
            size_t estimatedGpuDrawCount = 0;
            size_t debugLineCount = 0;
            size_t xrayLineCount = 0;
            uint32_t lightProbeDrawnPoints = 0;
            uint32_t lightProbeTotalPoints = 0;
            bool lightProbeCapped = false;
            bool frustumCullingEnabled = false;

            bool ssaoEnabled = false;
            bool ssaoValid = false;
            bool ssaoSuppressed = false;
            SsaoMode ssaoMode = SsaoMode::Off;
            bool ssaoGeometryBufferEnabled = false;
            bool ssaoGeometryBufferWritten = false;
            DXGI_FORMAT ssaoGeometryBufferFormat = DXGI_FORMAT_UNKNOWN;
            uint32_t ssaoReferenceSampleCount = 0;
            uint32_t ssaoReferenceBlurIterations = 0;
            uint32_t ssaoSampleCount = 0;
            uint32_t ssaoBlurIterations = 0;
            uint32_t ssaoWidth = 0;
            uint32_t ssaoHeight = 0;
            uint32_t ssaoInternalWidth = 0;
            uint32_t ssaoInternalHeight = 0;
            bool ssaoHalfResolution = false;
            uint64_t ssaoScreenPixelCount = 0;
            uint64_t ssaoInternalPixelCount = 0;
            float ssaoPixelSavedPercent = 0.0f;
            bool ssaoPixMarkersAvailable = false;
            bool ssaoGpuTimingAvailable = false;
            bool ssaoMainGpuValid = false;
            bool ssaoBlurGpuValid = false;
            double ssaoMainGpuMs = 0.0;
            double ssaoBlurGpuMs = 0.0;
            double ssaoTotalGpuMs = 0.0;
            float ssaoGeometryCpuMs = 0.0f;
            float ssaoMainCpuMs = 0.0f;
            float ssaoBlurCpuMs = 0.0f;
            float ssaoCompositeCpuMs = 0.0f;
            float ssaoTotalCpuMs = 0.0f;
            float shadowToForwardRatio = 0.0f;
            GFX::GPU_PROFILE::FrameSnapshot gpuProfile{};
        };

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

        int PenaltyAbove(float value, float start, float range, int maxPenalty) {
            if (value <= start || range <= 0.0f) {
                return 0;
            }
            return ClampScore(((value - start) / range) * static_cast<float>(maxPenalty));
        }

        int RatioPenalty(float ratio, int maxPenalty) {
            return ClampScore(ratio * static_cast<float>(maxPenalty));
        }

        int WeightedAverage(float total, float weight) {
            return weight > 0.0f ? ClampScore(total / weight) : 0;
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

        const char* SsaoInputText(const AuditSnapshot& audit) {
            if (!audit.ssaoEnabled || audit.ssaoSuppressed || audit.ssaoMode == SsaoMode::Off) {
                return "Off";
            }
            return "GeometryBuffer";
        }

        uint64_t PixelCount(uint32_t width, uint32_t height) {
            return static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        }

        const GFX::GPU_PROFILE::PassTiming& PassTimingFor(
            const GFX::GPU_PROFILE::FrameSnapshot& profile,
            GFX::GPU_PROFILE::Pass pass) {

            return profile.passes[static_cast<size_t>(pass)];
        }

        void DrawScoreMeter(
            const char* label,
            int score,
            const char* band,
            ImVec4 color,
            const char* note) {
            ImGui::PushID(label);
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::TextColored(color, "%d / 100 (%s)", score, band);

            char overlay[32]{};
            std::snprintf(overlay, sizeof(overlay), "%d", score);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
            ImGui::ProgressBar(static_cast<float>(score) / 100.0f, ImVec2(-1.0f, 0.0f), overlay);
            ImGui::PopStyleColor();
            ImGui::TextWrapped("%s", note);
            ImGui::PopID();
        }

        void DrawScoreRow(const char* label, int score, const char* detail) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ScoreBandColor(score), "%d / 100", score);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", detail);
        }

        void UpdateDominantRisk(
            const char*& outName,
            int& outRisk,
            const char* name,
            int risk) {
            if (risk > outRisk) {
                outName = name;
                outRisk = risk;
            }
        }

        AuditSnapshot BuildAuditSnapshot() {
            const FrameContext& frame = TIME::GetFrameContext();
            const MODELRENDERER::ModelRendererDebugStats& modelStats = MODELRENDERER::GetDebugStats();
            const MODELRENDERER::ModelRendererFrameStats& modelFrameStats = modelStats.frame;
            const RenderSubmissionDebugStats& renderSubmissionStats =
                RenderSubmissionSystem::GetDebugStats();
            const MESHRENDERER::MeshRendererDebugStats& meshStats = MESHRENDERER::GetDebugStats();
            const SHADOW::ShadowMapDebugStats& shadowStats = SHADOW::GetDebugStats();
            const RENDERER3D::DEBUG::DebugRendererFrameStats& debugStats =
                RENDERER3D::DEBUG::GetDebugRendererFrameStats();
            const RENDER3D::SCREENSPACE::SsaoDebugState& ssaoStats =
                RENDER3D::SCREENSPACE::GetSsaoDebugState();

            AuditSnapshot audit{};
            audit.gpuProfile = GFX::GPU_PROFILE::GetLatestSnapshot();
            audit.fpsRaw = frame.rawDt > 0.0f ? (1.0f / frame.rawDt) : 0.0f;
            audit.frustumCullingEnabled = renderSubmissionStats.frustumCullingEnabled;
            audit.scannedModelCount = renderSubmissionStats.scannedModelCount;
            audit.hiddenModelCount = renderSubmissionStats.hiddenModelCount;
            audit.culledModelCount = renderSubmissionStats.culledModelCount;
            audit.missingBoundsCount = renderSubmissionStats.missingBoundsCount;
            audit.skinnedCullSkippedCount = renderSubmissionStats.skinnedCullSkippedCount;
            audit.submittedModelCount = modelFrameStats.submittedModelItemCount;
            audit.structuredNodeCount = modelFrameStats.structuredNodeSubmittedCount;
            audit.matrixBuildCount = modelFrameStats.nodeGlobalMatrixBuildCount;

            audit.sceneSubmissionHeadroom = 100;
            audit.sceneSubmissionHeadroom -= PenaltyAbove(
                static_cast<float>(audit.scannedModelCount),
                64.0f,
                512.0f,
                20);
            audit.sceneSubmissionHeadroom -= audit.frustumCullingEnabled ? 0 : 12;
            audit.sceneSubmissionHeadroom -= PenaltyAbove(
                static_cast<float>(audit.missingBoundsCount),
                0.0f,
                16.0f,
                22);
            audit.sceneSubmissionHeadroom -= PenaltyAbove(
                static_cast<float>(audit.skinnedCullSkippedCount),
                0.0f,
                32.0f,
                8);
            audit.sceneSubmissionHeadroom = ClampScore(static_cast<float>(audit.sceneSubmissionHeadroom));

            audit.modelRendererHeadroom = 100;
            audit.modelRendererHeadroom -= PenaltyAbove(
                static_cast<float>(audit.structuredNodeCount),
                0.0f,
                160.0f,
                45);
            audit.modelRendererHeadroom -= PenaltyAbove(
                static_cast<float>(audit.matrixBuildCount),
                0.0f,
                48.0f,
                18);
            audit.modelRendererHeadroom -= PenaltyAbove(
                static_cast<float>(audit.submittedModelCount),
                0.0f,
                24.0f,
                18);
            audit.modelRendererHeadroom -= modelFrameStats.renderModelInvalidRequestCount > 0u ? 10 : 0;
            audit.modelRendererHeadroom -= audit.missingBoundsCount > 0 ? 10 : 0;
            audit.modelRendererHeadroom = ClampScore(static_cast<float>(audit.modelRendererHeadroom));

            audit.cpuSubmissionHeadroom = WeightedAverage(
                audit.sceneSubmissionHeadroom * 0.90f +
                    audit.modelRendererHeadroom * 1.10f,
                2.00f);

            audit.forwardDrawCount = meshStats.staticDrawItemCount + meshStats.skinnedDrawItemCount;
            audit.shadowDrawCount = shadowStats.totalPrimitiveCasterDrawCount;
            audit.estimatedGpuDrawCount =
                audit.forwardDrawCount +
                audit.shadowDrawCount +
                meshStats.wireGpuDrawCount;
            audit.shadowToForwardRatio = SafeRatio(audit.shadowDrawCount, audit.forwardDrawCount);

            audit.forwardHeadroom = 100;
            audit.forwardHeadroom -= PenaltyAbove(static_cast<float>(audit.estimatedGpuDrawCount), 64.0f, 256.0f, 40);
            audit.forwardHeadroom -= PenaltyAbove(static_cast<float>(audit.forwardDrawCount), 48.0f, 192.0f, 20);
            audit.forwardHeadroom -= meshStats.psoCacheMissCount > 0u ? 18 : 0;
            audit.forwardHeadroom -= meshStats.materialTextureCacheMissCount > 0u ? 12 : 0;
            audit.forwardHeadroom -= meshStats.normalTextureCacheMissCount > 0u ? 8 : 0;
            audit.forwardHeadroom -= audit.frustumCullingEnabled ? 0 : 12;
            audit.forwardHeadroom -= audit.missingBoundsCount > 0 ? 10 : 0;
            audit.forwardHeadroom = ClampScore(static_cast<float>(audit.forwardHeadroom));

            audit.shadowHeadroom = shadowStats.enabled ? 100 : 95;
            if (shadowStats.enabled) {
                audit.shadowHeadroom -= PenaltyAbove(audit.shadowToForwardRatio, 0.75f, 1.25f, 45);
                audit.shadowHeadroom -= PenaltyAbove(static_cast<float>(audit.shadowDrawCount), 32.0f, 160.0f, 25);
                audit.shadowHeadroom -= RatioPenalty(
                    SafeRatio(shadowStats.alphaMaskCasterDrawCount, audit.shadowDrawCount),
                    12);
            }
            audit.shadowHeadroom = ClampScore(static_cast<float>(audit.shadowHeadroom));

            audit.debugLineCount = debugStats.expandedLineCount;
            audit.xrayLineCount = debugStats.xrayLineCount;
            audit.lightProbeDrawnPoints = debugStats.lightProbeGizmoDrawnPointCount;
            audit.lightProbeTotalPoints = debugStats.lightProbeGizmoTotalPointCount;
            audit.lightProbeCapped = debugStats.lightProbeGizmoCapped;
            audit.debugOverlayHeadroom = 100;
            audit.debugOverlayHeadroom -= PenaltyAbove(static_cast<float>(audit.debugLineCount), 256.0f, 2048.0f, 45);
            audit.debugOverlayHeadroom -= RatioPenalty(SafeRatio(audit.xrayLineCount, audit.debugLineCount), 22);
            audit.debugOverlayHeadroom -= PenaltyAbove(static_cast<float>(audit.lightProbeDrawnPoints), 64.0f, 512.0f, 18);
            audit.debugOverlayHeadroom = ClampScore(static_cast<float>(audit.debugOverlayHeadroom));

            audit.ssaoEnabled = ssaoStats.enabled;
            audit.ssaoValid = ssaoStats.valid;
            audit.ssaoSuppressed = ssaoStats.suppressed;
            audit.ssaoMode = ssaoStats.mode;
            audit.ssaoGeometryBufferEnabled = ssaoStats.geometryBufferEnabled;
            audit.ssaoGeometryBufferWritten = ssaoStats.geometryBufferWritten;
            audit.ssaoGeometryBufferFormat = ssaoStats.geometryBufferFormat;
            audit.ssaoReferenceSampleCount = ssaoStats.referenceSampleCount;
            audit.ssaoReferenceBlurIterations = ssaoStats.referenceBlurIterations;
            audit.ssaoSampleCount = ssaoStats.sampleCount;
            audit.ssaoBlurIterations = ssaoStats.blurIterations;
            audit.ssaoWidth = ssaoStats.width;
            audit.ssaoHeight = ssaoStats.height;
            audit.ssaoInternalWidth = ssaoStats.internalWidth;
            audit.ssaoInternalHeight = ssaoStats.internalHeight;
            audit.ssaoHalfResolution = ssaoStats.halfResolution;
            audit.ssaoScreenPixelCount = PixelCount(ssaoStats.width, ssaoStats.height);
            audit.ssaoInternalPixelCount = PixelCount(ssaoStats.internalWidth, ssaoStats.internalHeight);
            if (audit.ssaoScreenPixelCount > 0u && audit.ssaoInternalPixelCount <= audit.ssaoScreenPixelCount) {
                const float activeRatio = SafeRatio(audit.ssaoInternalPixelCount, audit.ssaoScreenPixelCount);
                audit.ssaoPixelSavedPercent = (1.0f - activeRatio) * 100.0f;
            }
            audit.ssaoPixMarkersAvailable = ssaoStats.pixMarkersAvailable;
            audit.ssaoGpuTimingAvailable = audit.gpuProfile.gpuTimingAvailable;
            const GFX::GPU_PROFILE::PassTiming& ssaoMainGpu =
                PassTimingFor(audit.gpuProfile, GFX::GPU_PROFILE::Pass::SsaoMain);
            const GFX::GPU_PROFILE::PassTiming& ssaoBlurGpu =
                PassTimingFor(audit.gpuProfile, GFX::GPU_PROFILE::Pass::SsaoBlur);
            audit.ssaoMainGpuValid = ssaoMainGpu.valid;
            audit.ssaoBlurGpuValid = ssaoBlurGpu.valid;
            audit.ssaoMainGpuMs = ssaoMainGpu.valid ? ssaoMainGpu.gpuMs : 0.0;
            audit.ssaoBlurGpuMs = ssaoBlurGpu.valid ? ssaoBlurGpu.gpuMs : 0.0;
            audit.ssaoTotalGpuMs = audit.ssaoMainGpuMs + audit.ssaoBlurGpuMs;
            audit.ssaoGeometryCpuMs = ssaoStats.geometryBufferCpuMs;
            audit.ssaoMainCpuMs = ssaoStats.mainCpuMs;
            audit.ssaoBlurCpuMs = ssaoStats.blurCpuMs;
            audit.ssaoCompositeCpuMs = ssaoStats.compositeCpuMs;
            audit.ssaoTotalCpuMs = ssaoStats.totalCpuMs;
            audit.ssaoHeadroom = 100;
            if (ssaoStats.enabled && !ssaoStats.suppressed) {
                audit.ssaoHeadroom -= PenaltyAbove(static_cast<float>(ssaoStats.sampleCount), 8.0f, 24.0f, 35);
                audit.ssaoHeadroom -= PenaltyAbove(static_cast<float>(ssaoStats.blurIterations), 1.0f, 3.0f, 25);
                audit.ssaoHeadroom -= ssaoStats.geometryBufferEnabled ? 12 : 0;
                audit.ssaoHeadroom -= ssaoStats.valid ? 0 : 25;
                const uint64_t pixelCount =
                    static_cast<uint64_t>(ssaoStats.internalWidth) * static_cast<uint64_t>(ssaoStats.internalHeight);
                audit.ssaoHeadroom -= PenaltyAbove(static_cast<float>(pixelCount), 921600.0f, 2073600.0f, 12);
            }
            audit.ssaoHeadroom = ClampScore(static_cast<float>(audit.ssaoHeadroom));

            audit.passHeadroom = WeightedAverage(
                audit.forwardHeadroom * 1.05f +
                    audit.shadowHeadroom * 1.15f +
                    audit.ssaoHeadroom * 1.00f +
                    audit.debugOverlayHeadroom * 0.55f,
                3.75f);
            audit.performanceHeadroom = WeightedAverage(
                audit.cpuSubmissionHeadroom * 0.85f +
                    audit.passHeadroom * 1.15f,
                2.00f);

            UpdateDominantRisk(
                audit.dominantRiskName,
                audit.dominantRisk,
                "Scene Submission",
                100 - audit.sceneSubmissionHeadroom);
            UpdateDominantRisk(
                audit.dominantRiskName,
                audit.dominantRisk,
                "ModelRenderer Residual",
                100 - audit.modelRendererHeadroom);
            UpdateDominantRisk(
                audit.dominantRiskName,
                audit.dominantRisk,
                "Forward Draw / Binding",
                100 - audit.forwardHeadroom);
            UpdateDominantRisk(
                audit.dominantRiskName,
                audit.dominantRisk,
                "Shadow Pass",
                100 - audit.shadowHeadroom);
            UpdateDominantRisk(
                audit.dominantRiskName,
                audit.dominantRisk,
                "SSAO / Post",
                100 - audit.ssaoHeadroom);
            UpdateDominantRisk(
                audit.dominantRiskName,
                audit.dominantRisk,
                "Debug Overlay",
                100 - audit.debugOverlayHeadroom);

            return audit;
        }

        void DrawTopSummary(const AuditSnapshot& audit) {
            if (ImGui::BeginTable("PerformanceAuditSummary", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                DrawScoreMeter(
                    "CPU Submission",
                    audit.cpuSubmissionHeadroom,
                    ScoreBandText(audit.cpuSubmissionHeadroom),
                    ScoreBandColor(audit.cpuSubmissionHeadroom),
                    "Scene iteration and legacy ModelRenderer residual work.");
                ImGui::TableNextColumn();
                DrawScoreMeter(
                    "Pass Headroom",
                    audit.passHeadroom,
                    ScoreBandText(audit.passHeadroom),
                    ScoreBandColor(audit.passHeadroom),
                    "Forward, shadow, SSAO and debug overlay counter pressure.");
                ImGui::TableNextColumn();
                DrawScoreMeter(
                    "Overall Headroom",
                    audit.performanceHeadroom,
                    ScoreBandText(audit.performanceHeadroom),
                    ScoreBandColor(audit.performanceHeadroom),
                    "Stable frame-cost signals without migration-path takeover scoring.");
                ImGui::EndTable();
            }
        }

        void DrawRiskVerdict(const AuditSnapshot& audit) {
            ImGui::SeparatorText("Verdict");
            ImGui::Text("Dominant Risk: ");
            ImGui::SameLine();
            ImGui::TextColored(ScoreBandColor(100 - audit.dominantRisk), "%s (%d)", audit.dominantRiskName, audit.dominantRisk);

            if (!audit.frustumCullingEnabled) {
                ImGui::BulletText("Scene submission is running without an active render camera for culling.");
            }
            if (audit.cpuSubmissionHeadroom < 60) {
                ImGui::BulletText("CPU-side submission counters are high enough to inspect scene iteration and ModelRenderer work.");
            }
            if (audit.ssaoEnabled && !audit.ssaoSuppressed && audit.ssaoHeadroom < 60) {
                ImGui::BulletText("SSAO is running with relatively heavy settings for the editor viewport.");
            }
            if ((audit.ssaoMode == SsaoMode::OptimizedHigh || audit.ssaoMode == SsaoMode::Balanced) &&
                audit.ssaoEnabled &&
                !audit.ssaoSuppressed &&
                !audit.ssaoHalfResolution) {
                ImGui::BulletText("SSAO optimized mode is active without half-resolution work pixels.");
            }
            if (audit.shadowToForwardRatio > 1.20f) {
                ImGui::BulletText("Shadow pass draw pressure is higher than the forward pass.");
            }
            if (audit.forwardDrawCount > 0u && audit.estimatedGpuDrawCount >= audit.forwardDrawCount * 2u) {
                ImGui::BulletText("Frame work is duplicated across passes; pass count may matter more than submit caching.");
            }
            if (audit.debugOverlayHeadroom < 70) {
                ImGui::BulletText("Debug overlays are visible enough to affect editor measurements.");
            }
            if (audit.dominantRisk < 20) {
                ImGui::BulletText("No strong counter-based risk is visible; use PIX/timers for finer pass timing.");
            }
        }

        void DrawStageTable(const AuditSnapshot& audit) {
            ImGui::SeparatorText("Stages");
            if (ImGui::BeginTable(
                    "PerformanceAuditStages",
                    3,
                    ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Signal");
                ImGui::TableHeadersRow();

                char detail[288]{};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Scanned %d, hidden %d, culled %d, frustum %s, missing bounds %d",
                    audit.scannedModelCount,
                    audit.hiddenModelCount,
                    audit.culledModelCount,
                    audit.frustumCullingEnabled ? "on" : "off",
                    audit.missingBoundsCount);
                DrawScoreRow("Scene Submission", audit.sceneSubmissionHeadroom, detail);

                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Submitted models %d, structured nodes %d, matrix builds %d",
                    audit.submittedModelCount,
                    audit.structuredNodeCount,
                    audit.matrixBuildCount);
                DrawScoreRow("ModelRenderer Residual", audit.modelRendererHeadroom, detail);

                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Estimated draws %zu, forward draws %zu, PSO miss and texture miss are included",
                    audit.estimatedGpuDrawCount,
                    audit.forwardDrawCount);
                DrawScoreRow("Forward / Binding", audit.forwardHeadroom, detail);

                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Shadow draws %zu, ratio %.2f vs forward",
                    audit.shadowDrawCount,
                    audit.shadowToForwardRatio);
                DrawScoreRow("Shadow Pass", audit.shadowHeadroom, detail);

                std::snprintf(
                    detail,
                    sizeof(detail),
                    "%s, %s, valid %s, samples %u, blur %u, AO %u x %u%s, saved %.1f%%, %s",
                    RENDER3D::SCREENSPACE::ToString(audit.ssaoMode),
                    SsaoInputText(audit),
                    audit.ssaoValid ? "yes" : "no",
                    audit.ssaoSampleCount,
                    audit.ssaoBlurIterations,
                    audit.ssaoInternalWidth,
                    audit.ssaoInternalHeight,
                    audit.ssaoHalfResolution ? " half" : "",
                    audit.ssaoPixelSavedPercent,
                    (audit.ssaoMainGpuValid || audit.ssaoBlurGpuValid) ? "GPU timed" : "GPU waiting");
                DrawScoreRow("SSAO / Post", audit.ssaoHeadroom, detail);

                std::snprintf(
                    detail,
                    sizeof(detail),
                    "Lines %zu, xray %zu, light probe points %u / %u%s",
                    audit.debugLineCount,
                    audit.xrayLineCount,
                    audit.lightProbeDrawnPoints,
                    audit.lightProbeTotalPoints,
                    audit.lightProbeCapped ? " capped" : "");
                DrawScoreRow("Debug Overlay", audit.debugOverlayHeadroom, detail);

                ImGui::EndTable();
            }
        }

        void DrawGpuTimingEvidence(const AuditSnapshot& audit) {
            ImGui::SeparatorText("GPU Timing");
            const GFX::GPU_PROFILE::FrameSnapshot& profile = audit.gpuProfile;
            if (!profile.gpuTimingAvailable) {
                if (profile.profilerEnabled) {
                    ImGui::TextDisabled("Timestamp query data is not available yet.");
                } else {
                    ImGui::TextDisabled("%s", profile.unavailableReason);
                }
                return;
            }

            double totalMs = 0.0;
            int validCount = 0;
            for (const GFX::GPU_PROFILE::PassTiming& timing : profile.passes) {
                if (timing.valid) {
                    totalMs += timing.gpuMs;
                    ++validCount;
                }
            }

            ImGui::Text("Frame %llu, frequency %llu Hz, measured passes %d, sum %.3f ms",
                static_cast<unsigned long long>(profile.frameIndex),
                static_cast<unsigned long long>(profile.timestampFrequency),
                validCount,
                totalMs);
            ImGui::TextDisabled("GPU timings are delayed readback values and may lag by a few frames.");

            if (ImGui::BeginTable(
                    "PerformanceAuditGpuTiming",
                    3,
                    ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Ticks");
                ImGui::TableHeadersRow();

                for (const GFX::GPU_PROFILE::PassTiming& timing : profile.passes) {
                    if (!timing.valid) {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(timing.name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", timing.gpuMs);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", static_cast<unsigned long long>(timing.ticks));
                }

                ImGui::EndTable();
            }
        }

        void DrawSsaoEvidence(const AuditSnapshot& audit) {
            ImGui::SeparatorText("SSAO Evidence");
            if (ImGui::BeginTable(
                    "PerformanceAuditSsaoEvidence",
                    2,
                    ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Mode / Resolution");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s, %s, screen %u x %u, AO %u x %u%s, valid %s%s",
                    RENDER3D::SCREENSPACE::ToString(audit.ssaoMode),
                    SsaoInputText(audit),
                    audit.ssaoWidth,
                    audit.ssaoHeight,
                    audit.ssaoInternalWidth,
                    audit.ssaoInternalHeight,
                    audit.ssaoHalfResolution ? " half" : "",
                    audit.ssaoValid ? "yes" : "no",
                    audit.ssaoSuppressed ? ", suppressed" : "");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Pixel Budget");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Screen %llu px, AO work %llu px, saved %.1f%%",
                    static_cast<unsigned long long>(audit.ssaoScreenPixelCount),
                    static_cast<unsigned long long>(audit.ssaoInternalPixelCount),
                    audit.ssaoPixelSavedPercent);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Reference / Active");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Reference %u samples / %u blur, active %u samples / %u blur",
                    audit.ssaoReferenceSampleCount,
                    audit.ssaoReferenceBlurIterations,
                    audit.ssaoSampleCount,
                    audit.ssaoBlurIterations);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("GeometryBuffer");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s, written %s, format %s, CPU %.3f ms",
                    audit.ssaoGeometryBufferEnabled ? "enabled" : "off",
                    audit.ssaoGeometryBufferWritten ? "yes" : "no",
                    GFX::FormatToString(audit.ssaoGeometryBufferFormat),
                    audit.ssaoGeometryCpuMs);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("SSAO CPU Record");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Main %.3f ms, Blur %.3f ms, Composite %.3f ms, Total %.3f ms",
                    audit.ssaoMainCpuMs,
                    audit.ssaoBlurCpuMs,
                    audit.ssaoCompositeCpuMs,
                    audit.ssaoTotalCpuMs);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("SSAO GPU Record");
                ImGui::TableSetColumnIndex(1);
                if (audit.ssaoMainGpuValid || audit.ssaoBlurGpuValid) {
                    ImGui::Text("Main %.3f ms%s, Blur/Resolve %.3f ms%s, Total %.3f ms",
                        audit.ssaoMainGpuMs,
                        audit.ssaoMainGpuValid ? "" : " missing",
                        audit.ssaoBlurGpuMs,
                        audit.ssaoBlurGpuValid ? "" : " missing",
                        audit.ssaoTotalGpuMs);
                } else {
                    ImGui::TextDisabled("waiting for timestamp query readback");
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("PIX / GPU Timing");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("PIX marker %s, GPU timing %s",
                    audit.ssaoPixMarkersAvailable ? "available" : "missing",
                    audit.ssaoGpuTimingAvailable ? "available" : "PIX required");

                ImGui::EndTable();
            }

            ImGui::TextDisabled("Composite is applied in ForwardOpaque; no standalone SSAO composite pass is inserted.");
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
        const AuditSnapshot audit = BuildAuditSnapshot();

        ImGui::Text("FPS(raw): %.1f", audit.fpsRaw);
        ImGui::SameLine();
        ImGui::TextDisabled("Counters are heuristic signals; GPU timings are delayed timestamp queries.");

        DrawTopSummary(audit);
        DrawRiskVerdict(audit);
        DrawStageTable(audit);
        DrawGpuTimingEvidence(audit);
        DrawSsaoEvidence(audit);
#endif
    }

} // namespace HIKARI
