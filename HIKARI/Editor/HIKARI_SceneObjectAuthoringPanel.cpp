#include "HIKARI_SceneObjectAuthoringPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "HIKARI_EditorContext.h"
#include "HIKARI_SelectionSyncService.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {
    namespace {
        std::string SanitizePrefabToken(const std::string& raw) {
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
            if (sanitized.empty()) {
                return "NewPrefab";
            }
            return sanitized;
        }
    }

    void SceneObjectAuthoringPanel::Draw(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Object Authoring")) {
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

    void SceneObjectAuthoringPanel::DrawContents(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) {
#if defined(_DEBUG)

        if (ImGui::Button("Create Object")) {
            SceneObjectData newObject{};
            newObject.id = SceneObjectId{ context.nextSceneObjectId++ };
            newObject.name = "GameObject_" + std::to_string(newObject.id.value);
            scene.GetSceneDocument().objects.push_back(newObject);
            context.sceneDirty = true;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
        }

        if (context.selection.selectedObject != nullptr) {
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected")) {
                SceneObjectData* target = selectionSync.FindDocumentObjectByRuntime(scene, context.selection.selectedObject);
                if (target != nullptr) {
                    const SceneObjectId targetId = target->id;
                    auto& objects = scene.GetSceneDocument().objects;
                    objects.erase(
                        std::remove_if(objects.begin(), objects.end(),
                            [targetId](const SceneObjectData& object) { return object.id == targetId; }),
                        objects.end());
                    context.selection.selectedObject = nullptr;
                    context.selection.selectedAsset = nullptr;
                    context.sceneDirty = true;
                    selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                }
            }

            if (ImGui::Button("Duplicate Selected")) {
                if (SceneObjectData* target = selectionSync.FindDocumentObjectByRuntime(scene, context.selection.selectedObject)) {
                    SceneObjectData duplicate = *target;
                    duplicate.id = SceneObjectId{ context.nextSceneObjectId++ };
                    duplicate.name = duplicate.name + "_Copy";
                    scene.GetSceneDocument().objects.push_back(std::move(duplicate));
                    context.sceneDirty = true;
                    selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                }
            }

            if (SceneObjectData* target = selectionSync.FindDocumentObjectByRuntime(scene, context.selection.selectedObject)) {
                char prefabNameBuffer[128]{};
                std::snprintf(prefabNameBuffer, sizeof(prefabNameBuffer), "%s", context.prefabNameBuffer.c_str());
                if (ImGui::InputText("Prefab ID", prefabNameBuffer, sizeof(prefabNameBuffer))) {
                    context.prefabNameBuffer = prefabNameBuffer;
                }

                if (ImGui::Button("Save Selected As Prefab")) {
                    const std::string prefabId = SanitizePrefabToken(context.prefabNameBuffer.empty() ? target->name : context.prefabNameBuffer);
                    PrefabDocument prefab{};
                    prefab.prefabName = prefabId;
                    prefab.rootObject = *target;
                    prefab.rootObject.parent.reset();
                    prefab.rootObject.sourcePrefabId.clear();
                    prefabRegistry_.Save(prefabId, prefab, prefabSerializer_);
                }

                std::vector<std::string> prefabIds = prefabRegistry_.ListPrefabIds();
                std::sort(prefabIds.begin(), prefabIds.end());
                if (!prefabIds.empty()) {
                    if (context.prefabNameBuffer.empty()) {
                        context.prefabNameBuffer = prefabIds.front();
                    }

                    if (ImGui::BeginCombo("Create From Prefab", context.prefabNameBuffer.c_str())) {
                        for (const std::string& prefabId : prefabIds) {
                            const bool selected = (context.prefabNameBuffer == prefabId);
                            if (ImGui::Selectable(prefabId.c_str(), selected)) {
                                context.prefabNameBuffer = prefabId;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::Button("Instantiate Prefab")) {
                        PrefabDocument prefab{};
                        if (prefabRegistry_.Load(context.prefabNameBuffer, prefab, prefabSerializer_)) {
                            SceneObjectData instance = prefab.rootObject;
                            instance.id = SceneObjectId{ context.nextSceneObjectId++ };
                            instance.parent.reset();
                            instance.sourcePrefabId = context.prefabNameBuffer;
                            if (instance.name.empty()) {
                                instance.name = prefab.prefabName;
                            }
                            scene.GetSceneDocument().objects.push_back(std::move(instance));
                            context.sceneDirty = true;
                            selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                        }
                    }
                }

                std::vector<std::string> componentTypes = scene.GetComponentRegistry().GetTypeNames();
                std::sort(componentTypes.begin(), componentTypes.end());
                bool needsRebuildAfterAdd = false;
                if (ImGui::BeginCombo("Add Component", "Select component type")) {
                    for (const std::string& typeName : componentTypes) {
                        if (ImGui::Selectable(typeName.c_str(), false)) {
                            ComponentAddResult addResult = componentAuthoringService_.AddComponent(scene.GetComponentRegistry(), *target, typeName);
                            context.componentAddStatusMessage = addResult.message;
                            context.componentAddStatusIsError = !addResult.success;
                            if (addResult.success && addResult.documentChanged) {
                                context.sceneDirty = true;
                                needsRebuildAfterAdd = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (needsRebuildAfterAdd) {
                    selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                }

                if (!context.componentAddStatusMessage.empty()) {
                    if (context.componentAddStatusIsError) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", context.componentAddStatusMessage.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.45f, 1.0f), "%s", context.componentAddStatusMessage.c_str());
                    }
                }

                ImGui::SeparatorText("Document Components");
                bool needsRebuild = false;
                const InspectorContext inspectorContext{
                    &scene.GetAssetRegistry(),
                    &scene.GetSceneCatalog()
                };

                for (size_t componentIndex = 0; componentIndex < target->components.size(); ++componentIndex) {
                    SceneComponentData& component = target->components[componentIndex];
                    ImGui::PushID(static_cast<int>(componentIndex));
                    if (!ImGui::TreeNode(component.type.c_str())) {
                        ImGui::PopID();
                        continue;
                    }

                    if (componentDocumentEditor_.DrawComponent(scene.GetComponentRegistry(), component, componentInspectorBuilder_, inspectorContext)) {
                        context.sceneDirty = true;
                        needsRebuild = true;
                    }

                    ImGui::TreePop();
                    ImGui::PopID();
                }

                if (needsRebuild) {
                    selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                }
            }
        }
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
#endif
    }

} // namespace HIKARI
