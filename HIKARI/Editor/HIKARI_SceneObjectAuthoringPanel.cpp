#include "HIKARI_SceneObjectAuthoringPanel.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string_view>
#include <utility>

#include "HIKARI_EditorContext.h"
#include "HIKARI_SelectionSyncService.h"
#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_EditorObjectPlacement.h"
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

        ImGui::SeparatorText("Quick Create");
        if (ImGui::Button("Empty Object")) {
            const bool dirtyBefore =
                context.sceneDirty || scene.HasUnsavedSceneChanges();
            const std::vector<SceneObjectData> beforeObjects =
                scene.GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera =
                scene.GetSceneDocument().camera;
            EDITOR::CreateObjectRequest request{};
            request.position = EDITOR::ComputeObjectPlacementInView(
                scene.GetCamera());
            GameObject* object = EDITOR::CreateEmptyObject(scene, request);
            context.selection.selectedObject = object;
            context.selection.selectedAsset = nullptr;
            context.selection.selectedAssetGuid.clear();
            context.selection.selectedAssetPath.clear();
            context.sceneDirty = true;
            selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
            historyRequest_ = SceneObjectAuthoringHistoryRequest{
                "Create Empty Object",
                beforeObjects,
                scene.GetSceneDocument().objects,
                beforeCamera,
                scene.GetSceneDocument().camera,
                dirtyBefore
            };
        }

        const struct {
            const char* label;
            ProceduralMeshKind kind;
        } primitiveButtons[] = {
            { "Plane", ProceduralMeshKind::Plane },
            { "Grid Plane", ProceduralMeshKind::GridPlane },
            { "Box", ProceduralMeshKind::Box },
            { "Sphere", ProceduralMeshKind::Sphere },
            { "Cylinder", ProceduralMeshKind::Cylinder },
            { "Capsule", ProceduralMeshKind::Capsule }
        };
        if (ImGui::BeginTable("PrimitiveQuickCreate", 3)) {
            for (const auto& primitive : primitiveButtons) {
                ImGui::TableNextColumn();
                if (ImGui::Button(
                        primitive.label,
                        ImVec2(-FLT_MIN, 0.0f))) {
                    primitiveCreationDialog_.Open(primitive.kind);
                }
            }
            ImGui::EndTable();
        }

        EDITOR::CreatePrimitiveRequest primitiveRequest{};
        if (primitiveCreationDialog_.Draw(primitiveRequest)) {
            const bool dirtyBefore =
                context.sceneDirty || scene.HasUnsavedSceneChanges();
            const std::vector<SceneObjectData> beforeObjects =
                scene.GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera =
                scene.GetSceneDocument().camera;
            primitiveRequest.object.position =
                EDITOR::ComputePrimitivePlacementInView(
                    scene.GetCamera(),
                    primitiveRequest.mesh);
            GameObject* object = EDITOR::CreatePrimitiveObject(
                scene, primitiveRequest);
            context.selection.selectedObject = object;
            context.selection.selectedAsset = nullptr;
            context.selection.selectedAssetGuid.clear();
            context.selection.selectedAssetPath.clear();
            context.sceneDirty = true;
            selectionSync.SyncNextSceneObjectId(
                scene, context.nextSceneObjectId);
            historyRequest_ = SceneObjectAuthoringHistoryRequest{
                std::string("Create ") + ToString(primitiveRequest.mesh.kind),
                beforeObjects,
                scene.GetSceneDocument().objects,
                beforeCamera,
                scene.GetSceneDocument().camera,
                dirtyBefore
            };
        }

        SceneObjectId collisionSelectedObject{};
        if (context.selection.selectedObject != nullptr) {
            collisionSelectedObject =
                context.selection.selectedObject->GetDocumentId();
        }
        if (ImGui::Button("Collision Setup...")) {
            collisionAuthoringDialog_.Open(
                collisionSelectedObject);
        }
        EDITOR::CollisionAuthoringRequest collisionRequest{};
        if (collisionAuthoringDialog_.Draw(collisionRequest)) {
            const bool dirtyBefore =
                context.sceneDirty ||
                scene.HasUnsavedSceneChanges();
            const std::vector<SceneObjectData> beforeObjects =
                scene.GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera =
                scene.GetSceneDocument().camera;
            const EDITOR::CollisionAuthoringResult result =
                collisionAuthoringService_.Apply(
                    scene.GetComponentRegistry(),
                    scene.GetSceneDocument(),
                    collisionRequest);
            context.componentAddStatusMessage = result.message;
            context.componentAddStatusIsError = !result.success;
            if (result.documentChanged) {
                context.sceneDirty = true;
                selectionSync.RebuildRuntimeWorldWithSelectionSync(
                    scene,
                    context.selection,
                    context.nextSceneObjectId);
                historyRequest_ = SceneObjectAuthoringHistoryRequest{
                    collisionRequest.scope ==
                            EDITOR::CollisionAuthoringScope::SceneGeometry
                        ? "Setup Scene Collision"
                        : "Setup Object Collision",
                    beforeObjects,
                    scene.GetSceneDocument().objects,
                    beforeCamera,
                    scene.GetSceneDocument().camera,
                    dirtyBefore
                };
            }
        }
        if (!context.componentAddStatusMessage.empty()) {
            if (context.componentAddStatusIsError) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "%s",
                    context.componentAddStatusMessage.c_str());
            } else {
                ImGui::TextColored(
                    ImVec4(0.45f, 1.0f, 0.45f, 1.0f),
                    "%s",
                    context.componentAddStatusMessage.c_str());
            }
        }

        if (context.selection.selectedObject != nullptr) {
            ImGui::SeparatorText("Selected Object");
            if (ImGui::Button("Delete Selected")) {
                SceneObjectData* target = selectionSync.FindDocumentObjectByRuntime(scene, context.selection.selectedObject);
                if (target != nullptr) {
                    const bool dirtyBefore =
                        context.sceneDirty || scene.HasUnsavedSceneChanges();
                    const std::vector<SceneObjectData> beforeObjects =
                        scene.GetSceneDocument().objects;
                    const SceneCameraSettings beforeCamera =
                        scene.GetSceneDocument().camera;
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
                    historyRequest_ = SceneObjectAuthoringHistoryRequest{
                        "Delete Object",
                        beforeObjects,
                        scene.GetSceneDocument().objects,
                        beforeCamera,
                        scene.GetSceneDocument().camera,
                        dirtyBefore
                    };
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Duplicate Selected")) {
                if (SceneObjectData* target = selectionSync.FindDocumentObjectByRuntime(scene, context.selection.selectedObject)) {
                    const bool dirtyBefore =
                        context.sceneDirty || scene.HasUnsavedSceneChanges();
                    const std::vector<SceneObjectData> beforeObjects =
                        scene.GetSceneDocument().objects;
                    const SceneCameraSettings beforeCamera =
                        scene.GetSceneDocument().camera;
                    SceneObjectData duplicate = *target;
                    duplicate.id = SceneObjectId{ context.nextSceneObjectId++ };
                    duplicate.name = duplicate.name + "_Copy";
                    scene.GetSceneDocument().objects.push_back(std::move(duplicate));
                    context.sceneDirty = true;
                    selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                    historyRequest_ = SceneObjectAuthoringHistoryRequest{
                        "Duplicate Object",
                        beforeObjects,
                        scene.GetSceneDocument().objects,
                        beforeCamera,
                        scene.GetSceneDocument().camera,
                        dirtyBefore
                    };
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
                            const bool dirtyBefore =
                                context.sceneDirty || scene.HasUnsavedSceneChanges();
                            const std::vector<SceneObjectData> beforeObjects =
                                scene.GetSceneDocument().objects;
                            const SceneCameraSettings beforeCamera =
                                scene.GetSceneDocument().camera;
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
                            historyRequest_ = SceneObjectAuthoringHistoryRequest{
                                "Instantiate Prefab",
                                beforeObjects,
                                scene.GetSceneDocument().objects,
                                beforeCamera,
                                scene.GetSceneDocument().camera,
                                dirtyBefore
                            };
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
                            const bool dirtyBefore =
                                context.sceneDirty || scene.HasUnsavedSceneChanges();
                            const std::vector<SceneObjectData> beforeObjects =
                                scene.GetSceneDocument().objects;
                            const SceneCameraSettings beforeCamera =
                                scene.GetSceneDocument().camera;
                            ComponentAddResult addResult = componentAuthoringService_.AddComponent(
                                scene.GetComponentRegistry(),
                                *target,
                                typeInfo->typeName);
                            context.componentAddStatusMessage = addResult.message;
                            context.componentAddStatusIsError = !addResult.success;
                            if (addResult.success && addResult.documentChanged) {
                                context.sceneDirty = true;
                                needsRebuildAfterAdd = true;
                                historyRequest_ = SceneObjectAuthoringHistoryRequest{
                                    "Add " + typeInfo->presentation.displayName,
                                    beforeObjects,
                                    scene.GetSceneDocument().objects,
                                    beforeCamera,
                                    scene.GetSceneDocument().camera,
                                    dirtyBefore
                                };
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (needsRebuildAfterAdd) {
                    selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
                }

                DrawCameraAuthoring(
                    scene,
                    context,
                    *target,
                    openCinematicsWorkspaceCameraRequest_);
                if (std::optional<SceneObjectAuthoringHistoryRequest>
                        componentHistory =
                            componentAuthoringSection_.Draw(
                                scene,
                                context,
                                selectionSync,
                                *target)) {
                    historyRequest_ = std::move(componentHistory);
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

    std::optional<SceneObjectAuthoringHistoryRequest>
        SceneObjectAuthoringPanel::ConsumeHistoryRequest() {
        std::optional<SceneObjectAuthoringHistoryRequest> request =
            std::move(historyRequest_);
        historyRequest_.reset();
        return request;
    }

} // namespace HIKARI
