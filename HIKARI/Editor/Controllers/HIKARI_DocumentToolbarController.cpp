#include "Editor/HIKARI_DocumentToolbarController.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Authoring/Serialization/HIKARI_SceneSerializer.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {
    namespace {
        std::string SanitizeSceneToken(const std::string& raw) {
            std::string sanitized{};
            sanitized.reserve(raw.size());
            for (char ch : raw) {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (std::isalnum(c) != 0 || ch == '_' || ch == '-') {
                    sanitized.push_back(ch);
                } else if (!std::isspace(c)) {
                    sanitized.push_back('_');
                }
            }

            while (!sanitized.empty() && (sanitized.front() == '_' || sanitized.front() == '-')) {
                sanitized.erase(sanitized.begin());
            }
            while (!sanitized.empty() && (sanitized.back() == '_' || sanitized.back() == '-')) {
                sanitized.pop_back();
            }

            if (sanitized.empty()) {
                return "untitled";
            }
            return sanitized;
        }

        std::string BuildScenePath(const std::string& token) {
            return "Data/scenes/scene_" + token + ".json";
        }
    }

    void DocumentToolbarController::SyncDocumentMeta(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const {
        const SceneDocument& document = scene.GetSceneDocument();
        if (context.sceneNameEditBuffer != document.sceneName) {
            context.sceneNameEditBuffer = document.sceneName;
            if (!context.saveAsNameOverriddenByUser) {
                context.saveAsNameBuffer = document.sceneName;
            }
        }
        if (context.saveAsNameBuffer.empty()) {
            context.saveAsNameBuffer = document.sceneName;
            context.saveAsNameOverriddenByUser = false;
        }
        selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    }

    void DocumentToolbarController::Draw(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Document")) {
            ImGui::End();
            return;
        }

        DrawContents(scene, context, selectionSync);

        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
#endif
    }

    void DocumentToolbarController::DrawContents(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const {
#if defined(_DEBUG)

        char sceneNameBuffer[128]{};
        std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "%s", context.sceneNameEditBuffer.c_str());
        if (ImGui::InputText("Scene Name", sceneNameBuffer, sizeof(sceneNameBuffer))) {
            context.sceneNameEditBuffer = sceneNameBuffer;
            if (scene.GetSceneDocument().sceneName != context.sceneNameEditBuffer) {
                scene.GetSceneDocument().sceneName = context.sceneNameEditBuffer;
                if (!context.saveAsNameOverriddenByUser) {
                    context.saveAsNameBuffer = context.sceneNameEditBuffer;
                }
                context.sceneDirty = true;
            }
        }

        char saveAsBuffer[128]{};
        std::snprintf(saveAsBuffer, sizeof(saveAsBuffer), "%s", context.saveAsNameBuffer.c_str());
        if (ImGui::InputText("Save As Name", saveAsBuffer, sizeof(saveAsBuffer))) {
            context.saveAsNameBuffer = saveAsBuffer;
            context.saveAsNameOverriddenByUser = (context.saveAsNameBuffer != context.sceneNameEditBuffer);
        }

        auto saveSceneAsNewFile = [&]() {
            SceneDocument& sceneDocument = scene.GetSceneDocument();
            const std::string desiredName = context.saveAsNameBuffer.empty() ? sceneDocument.sceneName : context.saveAsNameBuffer;
            const std::string token = SanitizeSceneToken(desiredName);
            const std::string scenePath = BuildScenePath(token);
            sceneDocument.environment = scene.GetSceneEnvironment();
            SceneSerializer serializer{};
            if (serializer.SaveToFile(scenePath, sceneDocument)) {
                scene.SetScenePath(scenePath);
                scene.SetSceneId(token);
                scene.GetSceneCatalog().Register(SceneCatalogEntry{
                    scene.GetSceneId(),
                    "GameDocumentScene",
                    scene.GetScenePath(),
                    true,
                    scene.GetSceneId(),
                    SceneLifetimePolicy::ReloadOnEnter
                });
                context.sceneDirty = false;
                if (!desiredName.empty()) {
                    sceneDocument.sceneName = desiredName;
                    context.sceneNameEditBuffer = desiredName;
                } else {
                    sceneDocument.sceneName = token;
                    context.sceneNameEditBuffer = token;
                }
                context.saveAsNameBuffer = sceneDocument.sceneName;
                context.saveAsNameOverriddenByUser = false;
            }
        };

        if (ImGui::Button("Reload Assets")) {
            scene.ReloadAssets();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Scene")) {
            scene.ReloadSceneDocument();
            context.sceneDirty = false;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
            context.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameOverriddenByUser = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Scene")) {
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
            SceneSerializer serializer{};
            if (scene.GetScenePath().empty()) {
                saveSceneAsNewFile();
            } else if (serializer.SaveToFile(scene.GetScenePath(), scene.GetSceneDocument())) {
                context.sceneDirty = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As")) {
            saveSceneAsNewFile();
        }

        if (ImGui::Button("New Scene")) {
            scene.GetSceneDocument() = SceneDocument{};
            scene.GetSceneDocument().sceneName = "Untitled";
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
            scene.SetSceneId("Unsaved");
            scene.SetScenePath({});
            context.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameOverriddenByUser = false;
            context.nextSceneObjectId = 1;
            context.selection.selectedObject = nullptr;
            context.selection.selectedAsset = nullptr;
            context.sceneDirty = true;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
        }

        std::vector<std::string> sceneIds = scene.GetSceneCatalog().GetSceneIds();
        std::sort(sceneIds.begin(), sceneIds.end());
        if (!sceneIds.empty()) {
            int currentSceneIndex = 0;
            for (int i = 0; i < static_cast<int>(sceneIds.size()); ++i) {
                if (sceneIds[i] == scene.GetSceneId()) {
                    currentSceneIndex = i;
                    break;
                }
            }
            if (ImGui::BeginCombo("Scene Switcher", sceneIds[currentSceneIndex].c_str())) {
                for (int i = 0; i < static_cast<int>(sceneIds.size()); ++i) {
                    const bool selected = (i == currentSceneIndex);
                    if (ImGui::Selectable(sceneIds[i].c_str(), selected)) {
                        scene.SetSceneId(sceneIds[i]);
                        scene.ReloadSceneDocument();
                        context.sceneDirty = false;
                        selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                        context.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
                        context.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
                        context.saveAsNameOverriddenByUser = false;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Text("Scene: %s%s", scene.GetSceneDocument().sceneName.c_str(), context.sceneDirty ? "*" : "");
        ImGui::Text("Objects: %zu", scene.GetSceneDocument().objects.size());
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
#endif
    }

} // namespace HIKARI
