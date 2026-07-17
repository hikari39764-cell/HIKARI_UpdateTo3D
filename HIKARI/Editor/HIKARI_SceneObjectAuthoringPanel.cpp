#include "HIKARI_SceneObjectAuthoringPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>

#include "HIKARI_EditorContext.h"
#include "HIKARI_SelectionSyncService.h"
#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
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

        void SyncRuntimeTransformToDocument(SceneObjectData& target, const EditorContext& context) {
            if (!context.selection.selectedObject ||
                context.selection.selectedObject->GetDocumentId() != target.id) {
                return;
            }

            const Transform3D& transform = context.selection.selectedObject->Transform();
            target.transform.position = transform.position;
            target.transform.scale = transform.scale;
            target.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(transform.rotation);
        }

        bool ApplyDocumentComponentToRuntime(
            const SceneComponentData& componentData,
            size_t componentIndex,
            const EditorContext& context) {

            if (!context.selection.selectedObject) {
                return false;
            }

            const auto& runtimeComponents = context.selection.selectedObject->GetComponents();
            if (componentIndex >= runtimeComponents.size() || !runtimeComponents[componentIndex]) {
                return false;
            }

            IComponent* runtimeComponent = runtimeComponents[componentIndex].get();
            if (std::string(runtimeComponent->GetTypeName()) != componentData.type) {
                return false;
            }

            runtimeComponent->Deserialize(componentData.properties);
            return true;
        }

#if defined(HIKARI_WITH_EDITOR)
        int ComponentCategoryRank(std::string_view category) {
            if (category == "Rendering") {
                return 0;
            }
            if (category == "Camera") {
                return 1;
            }
            if (category == "Gameplay") {
                return 2;
            }
            if (category == "Cinematics") {
                return 3;
            }
            return 4;
        }

        bool ComponentPresentationLess(
            const ComponentTypeInfo* lhs,
            const ComponentTypeInfo* rhs) {

            if (lhs == nullptr || rhs == nullptr) {
                return rhs != nullptr;
            }
            const int lhsRank = ComponentCategoryRank(
                lhs->presentation.category);
            const int rhsRank = ComponentCategoryRank(
                rhs->presentation.category);
            if (lhsRank != rhsRank) {
                return lhsRank < rhsRank;
            }
            if (lhs->presentation.category !=
                rhs->presentation.category) {
                return lhs->presentation.category <
                    rhs->presentation.category;
            }
            return lhs->presentation.displayName <
                rhs->presentation.displayName;
        }

        bool HasDocumentComponent(const SceneObjectData& object, std::string_view typeName) {
            return std::any_of(
                object.components.begin(),
                object.components.end(),
                [typeName](const SceneComponentData& component) {
                    return component.type == typeName;
                });
        }

        void DrawCameraAuthoring(
            DocumentSceneBase& scene,
            EditorContext& context,
            const SceneObjectData& object,
            std::optional<SceneObjectId>& openCinematicsWorkspaceCameraRequest) {

            if (!HasDocumentComponent(object, "CameraComponent")) {
                return;
            }

            const bool isGameDefault =
                scene.GetSceneDocument().camera.defaultCameraObjectId == object.id;
            const bool previewingThisCamera =
                scene.IsEditorCameraPreviewActive() &&
                scene.GetEditorCameraPreviewObjectId() == object.id;

            ImGui::SeparatorText("Camera Authoring");
            ImGui::Text("Game Default: %s", isGameDefault ? "Yes" : "No");

            if (isGameDefault) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Set as Game Default")) {
                if (scene.SetGameDefaultCamera(object.id)) {
                    context.sceneDirty = true;
                }
            }
            if (isGameDefault) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (previewingThisCamera) {
                if (ImGui::Button("Exit Camera View")) {
                    scene.EndEditorCameraPreview();
                }
            } else if (ImGui::Button("View Through Camera")) {
                (void)scene.BeginEditorCameraPreview(object.id);
            }

            const bool snapDisabled = object.parent.has_value();
            if (snapDisabled) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Snap Camera to Current View")) {
                if (scene.SnapCameraObjectToEditorView(object.id)) {
                    context.sceneDirty = true;
                }
            }
            if (snapDisabled) {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Snap is available for root Camera objects only.");
            }

            if (ImGui::Button("Open in Cinematics Workspace")) {
                openCinematicsWorkspaceCameraRequest = object.id;
            }
        }
#endif
    }

    void SceneObjectAuthoringPanel::Draw(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) {
#if defined(HIKARI_WITH_EDITOR)
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
#if defined(HIKARI_WITH_EDITOR)

        if (ImGui::Button("Create Object")) {
            EDITOR::CreateObjectRequest request{};
            GameObject* object = EDITOR::CreateEmptyObject(scene, request);
            context.selection.selectedObject = object;
            context.selection.selectedAsset = nullptr;
            context.selection.selectedAssetGuid.clear();
            context.selection.selectedAssetPath.clear();
            context.sceneDirty = true;
            selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
        }

        if (context.selection.selectedObject != nullptr) {
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected")) {
                SceneObjectData* target = selectionSync.FindDocumentObjectByRuntime(scene, context.selection.selectedObject);
                if (target != nullptr) {
                    const SceneObjectId targetId = target->id;
                    if (scene.GetSceneDocument().camera.defaultCameraObjectId == targetId) {
                        scene.ClearGameDefaultCamera();
                    }
                    if (scene.IsEditorCameraPreviewActive() &&
                        scene.GetEditorCameraPreviewObjectId() == targetId) {
                        scene.EndEditorCameraPreview();
                    }
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

                std::vector<const ComponentTypeInfo*> componentTypes =
                    scene.GetComponentRegistry().GetTypeInfos();
                std::sort(
                    componentTypes.begin(),
                    componentTypes.end(),
                    ComponentPresentationLess);
                bool needsRebuildAfterAdd = false;
                if (ImGui::BeginCombo("Add Component", "Select component type")) {
                    std::string currentCategory{};
                    for (const ComponentTypeInfo* typeInfo : componentTypes) {
                        if (typeInfo == nullptr) {
                            continue;
                        }
                        if (currentCategory !=
                            typeInfo->presentation.category) {
                            if (!currentCategory.empty()) {
                                ImGui::Separator();
                            }
                            currentCategory =
                                typeInfo->presentation.category;
                            ImGui::TextDisabled(
                                "%s",
                                currentCategory.c_str());
                        }

                        ImGui::PushID(typeInfo->typeName.c_str());
                        const bool selected = ImGui::Selectable(
                            typeInfo->presentation.displayName.c_str(),
                            false);
                        if (ImGui::IsItemHovered() &&
                            !typeInfo->presentation.description.empty()) {
                            ImGui::SetTooltip(
                                "%s",
                                typeInfo->presentation.description.c_str());
                        }
                        ImGui::PopID();
                        if (selected) {
                            ComponentAddResult addResult = componentAuthoringService_.AddComponent(
                                scene.GetComponentRegistry(),
                                *target,
                                typeInfo->typeName);
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

                DrawCameraAuthoring(
                    scene,
                    context,
                    *target,
                    openCinematicsWorkspaceCameraRequest_);

                ImGui::SeparatorText("Document Components");
                bool needsRebuild = false;
                const InspectorContext inspectorContext{
                    &scene.GetAssetRegistry(),
                    &scene.GetAssetDatabase(),
                    &scene.GetSceneDocument()
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
                        const bool appliedToRuntime =
                            ApplyDocumentComponentToRuntime(component, componentIndex, context);
                        if (!appliedToRuntime) {
                            needsRebuild = true;
                        } else if (component.type == "CameraComponent") {
                            (void)scene.ApplyCameraRuntimeChanges();
                        }
                    }

                    ImGui::TreePop();
                    ImGui::PopID();
                }

                const bool editingComponentParameter = ImGui::IsAnyItemActive();
                if (needsRebuild) {
                    if (editingComponentParameter) {
                        deferredComponentRebuild_ = true;
                    } else {
                        SyncRuntimeTransformToDocument(*target, context);
                        selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                        deferredComponentRebuild_ = false;
                    }
                } else if (deferredComponentRebuild_ && !editingComponentParameter) {
                    SyncRuntimeTransformToDocument(*target, context);
                    selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                    deferredComponentRebuild_ = false;
                }
            }
        }
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
#endif
    }

    std::optional<SceneObjectId>
        SceneObjectAuthoringPanel::ConsumeOpenCinematicsWorkspaceCameraRequest() {
        std::optional<SceneObjectId> request = openCinematicsWorkspaceCameraRequest_;
        openCinematicsWorkspaceCameraRequest_.reset();
        return request;
    }

} // namespace HIKARI
