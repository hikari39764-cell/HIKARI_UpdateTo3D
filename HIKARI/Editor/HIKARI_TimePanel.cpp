#include "HIKARI_TimePanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    void TimePanel::DrawContents() const {
#if defined(_DEBUG)
        const FrameContext& frame = TIME::GetFrameContext();

        ImGui::Text("Frame Index: %llu", static_cast<unsigned long long>(frame.frameIndex));
        ImGui::Separator();

        ImGui::Text("rawDt:      %.6f", frame.rawDt);
        ImGui::Text("unscaledDt: %.6f", frame.unscaledDt);
        ImGui::Text("gameDt:     %.6f", frame.gameDt);
        ImGui::Text("fixedDt:    %.6f", frame.fixedDt);
        ImGui::Separator();

        ImGui::Text("timeScale:  %.3f", frame.gameTimeScale);
        ImGui::Text("paused:     %s", frame.paused ? "true" : "false");

        ImGui::Separator();
        ImGui::Text("FPS(raw):   %.1f", frame.rawDt > 0.0f ? (1.0f / frame.rawDt) : 0.0f);
        ImGui::Text("FPS(game):  %.1f", frame.gameDt > 0.0f ? (1.0f / frame.gameDt) : 0.0f);

        ImGui::Separator();
        ImGui::TextUnformatted("Quick Controls");

        if (ImGui::Button("Pause / Resume")) {
            TIME::SetPaused(!TIME::IsPaused());
        }
        ImGui::SameLine();
        if (ImGui::Button("TimeScale 1.0")) {
            TIME::SetGameTimeScale(1.0f);
        }

        if (ImGui::Button("TimeScale 0.1")) {
            TIME::SetGameTimeScale(0.1f);
        }
        ImGui::SameLine();
        if (ImGui::Button("TimeScale 2.0")) {
            TIME::SetGameTimeScale(2.0f);
        }

        const RenderSubmissionDebugStats& renderStats = RenderSubmissionSystem::GetDebugStats();
        MESHRENDERER::SubmissionRendererDebugOptions& renderDebug = MESHRENDERER::GetDebugOptions();

        ImGui::Separator();
        ImGui::TextUnformatted("Show Submission Debug");
        ImGui::Checkbox("Rotate Light", &renderDebug.rotateLight);
        ImGui::SameLine();
        ImGui::Checkbox("Use Normal", &renderDebug.useNormal);
        ImGui::SameLine();
        ImGui::Checkbox("Use Emissive", &renderDebug.useEmissive);

        ImGui::Text("Submitted Models: %d", renderStats.submittedModelCount);
        ImGui::Text("Draw Items:       %d", renderStats.submittedDrawItemCount);
        ImGui::Text("Fallback Wires:   %d", renderStats.fallbackWireCount);

        if (ImGui::TreeNode("Submission Items")) {
            for (size_t i = 0; i < renderStats.items.size(); ++i) {
                const SubmittedDrawItemDebugInfo& item = renderStats.items[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("[%zu] gpuMeshId=%u material=%llu", i, item.gpuMeshId, static_cast<unsigned long long>(item.material.id));
                ImGui::Text("hasBaseColor=%u hasNormal=%u hasOrm=%u hasEmissive=%u postMask=%u modelState=%u",
                    item.hasBaseColorTexture,
                    item.hasNormalTexture,
                    item.hasOrmTexture,
                    item.hasEmissiveTexture,
                    item.postGroupMask,
                    static_cast<unsigned>(item.modelState));
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
#endif
    }

} // namespace HIKARI
