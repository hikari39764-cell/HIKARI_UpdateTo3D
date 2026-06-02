#include "HIKARI_LightingBakePanel.h"

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#endif

namespace HIKARI {

    namespace {

        TOOLS::BAKING::LightingBakeRequest BuildLightingBakeRequest(DocumentSceneBase& scene) {
            TOOLS::BAKING::LightingBakeRequest request{};
            request.projectRoot = scene.GetAssetDatabase().GetProjectRoot();
            request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
            request.sceneName = scene.GetCurrentSceneDisplayName();
            request.scenePath = scene.GetScenePath();
            request.sceneDocument = &scene.GetSceneDocument();
            request.target = TOOLS::BAKING::LightingBakeTarget::All;
            return request;
        }

        const std::string& LastText(const std::vector<std::string>& values) {
            static const std::string empty{};
            return values.empty() ? empty : values.back();
        }

#if defined(_DEBUG)
        void DrawReportLines(const char* label, const std::vector<std::string>& lines, const ImVec4& color) {
            if (lines.empty()) {
                return;
            }

            ImGui::TextUnformatted(label);
            ImGui::Indent();
            for (const std::string& line : lines) {
                ImGui::TextColored(color, "%s", line.c_str());
            }
            ImGui::Unindent();
        }
#endif

        bool OpenFolderInShell(const std::filesystem::path& folder, std::string& outMessage) {
            std::error_code ec{};
            if (!std::filesystem::exists(folder, ec)) {
                outMessage = "Bake folder does not exist: " + folder.generic_string();
                return false;
            }

#if defined(_WIN32)
            const HINSTANCE result = ShellExecuteW(
                nullptr,
                L"open",
                folder.wstring().c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                outMessage = "Failed to open bake folder: " + folder.generic_string();
                return false;
            }

            outMessage = "Opened bake folder: " + folder.generic_string();
            return true;
#else
            outMessage = "Open folder is not implemented on this platform: " + folder.generic_string();
            return false;
#endif
        }

    } // namespace

    void LightingBakePanel::Draw(DocumentSceneBase& scene, bool& open) {
#if defined(_DEBUG)
        if (!ImGui::Begin("Lighting Bake", &open)) {
            ImGui::End();
            return;
        }

        const TOOLS::BAKING::LightingBakeRequest request = BuildLightingBakeRequest(scene);
        const std::filesystem::path bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            request.projectRoot,
            request.sceneGuid);
        const std::filesystem::path manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
            request.projectRoot,
            request.sceneGuid);

        ImGui::TextUnformatted("Current Scene");
        ImGui::Separator();
        ImGui::Text("Scene Name: %s", request.sceneName.empty() ? "(unnamed)" : request.sceneName.c_str());
        ImGui::Text("Scene GUID: %s", request.sceneGuid.empty() ? "(transient)" : request.sceneGuid.c_str());
        ImGui::TextWrapped("Scene Path: %s", request.scenePath.empty() ? "(unsaved)" : request.scenePath.c_str());
        ImGui::TextWrapped("Bake Root: %s", bakeRoot.generic_string().c_str());
        ImGui::TextWrapped("Manifest Path: %s", manifestPath.generic_string().c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted("Bake Actions");
        ImGui::Separator();

        // Bake UI は runtime へ直接依存を増やさず、Service 経由で操作する。
        TOOLS::BAKING::LightingBakeService service{};
        if (ImGui::Button("Validate Lighting Bake Setup")) {
            lastReport_ = service.ValidateLightingBakeSetup(request);
            hasReport_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Prepare Lighting Bake Manifest")) {
            lastReport_ = service.PrepareLightingBakeManifest(request);
            hasReport_ = true;
            scene.RefreshLightingRuntime();
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
                "LightingBake.PrepareManifest",
                &scene.GetSceneEnvironment());
        }
        if (ImGui::Button("Clear Lighting Bake")) {
            lastReport_ = service.ClearLightingBake(request);
            hasReport_ = true;
            scene.RefreshLightingRuntime();
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
                "LightingBake.Clear",
                &scene.GetSceneEnvironment());
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Bake Folder")) {
            if (OpenFolderInShell(bakeRoot, lastOpenFolderMessage_)) {
                HIKARI_LOG_INFO("[LightingBake] " + lastOpenFolderMessage_);
            } else {
                HIKARI_LOG_WARN("[LightingBake] " + lastOpenFolderMessage_);
            }
        }
        if (!lastOpenFolderMessage_.empty()) {
            ImGui::TextDisabled("%s", lastOpenFolderMessage_.c_str());
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Future Bake Targets");
        ImGui::Separator();
        ImGui::BulletText("Reflection Probe Baker: Not Implemented");
        ImGui::BulletText("Light Probe Baker: Not Implemented");
        ImGui::BulletText("Lightmap Baker: Not Implemented");

        ImGui::Spacing();
        ImGui::TextUnformatted("Last Report");
        ImGui::Separator();
        if (!hasReport_) {
            ImGui::TextDisabled("No bake action has been run.");
        } else {
            ImGui::Text("Action: %s", TOOLS::BAKING::ToString(lastReport_.action));
            ImGui::Text("Target: %s", TOOLS::BAKING::ToString(lastReport_.target));
            ImGui::Text("Result: %s", lastReport_.success ? "Success" : "Failed");
            ImGui::Text("Warnings: %zu", lastReport_.warnings.size());
            ImGui::Text("Errors: %zu", lastReport_.errors.size());
            ImGui::Text("Manifest Written: %s", lastReport_.manifestWritten ? "Yes" : "No");
            ImGui::Text("Folder Created: %s", lastReport_.bakeFolderCreated ? "Yes" : "No");
            ImGui::Text("Folder Cleared: %s", lastReport_.bakeFolderCleared ? "Yes" : "No");
            ImGui::Text("Reflection Probe Records: %u", lastReport_.reflectionProbeRecordCount);
            ImGui::Text("Light Probe Records: %u", lastReport_.lightProbeRecordCount);
            ImGui::Text("Lightmap Records: %u", lastReport_.lightmapRecordCount);

            const std::string& lastMessage = LastText(lastReport_.messages);
            if (!lastMessage.empty()) {
                ImGui::TextWrapped("Last Message: %s", lastMessage.c_str());
            }

            DrawReportLines("Warnings", lastReport_.warnings, ImVec4(1.0f, 0.82f, 0.35f, 1.0f));
            DrawReportLines("Errors", lastReport_.errors, ImVec4(1.0f, 0.42f, 0.35f, 1.0f));
        }

        ImGui::End();
#else
        (void)scene;
        (void)open;
#endif
    }

} // namespace HIKARI
