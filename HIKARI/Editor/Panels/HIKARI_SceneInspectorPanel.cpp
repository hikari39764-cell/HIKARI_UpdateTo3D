#include "Editor/Panels/HIKARI_SceneInspectorPanel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
        constexpr float kDegreesToRadians =
            3.14159265358979323846f / 180.0f;

        int ComponentCategoryRank(std::string_view category) {
            if (category == "Rendering") return 0;
            if (category == "Camera") return 1;
            if (category == "Gameplay") return 2;
            if (category == "Cinematics") return 3;
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

        bool HasDocumentComponent(
            const SceneObjectData& object,
            std::string_view typeName) {
            return std::any_of(
                object.components.begin(),
                object.components.end(),
                [typeName](const SceneComponentData& component) {
                    return component.type == typeName;
                });
        }

        std::string LowerCopy(std::string_view value) {
            std::string result(value);
            std::transform(
                result.begin(),
                result.end(),
                result.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
            return result;
        }

        bool MatchesComponent(
            const ComponentTypeInfo& info,
            std::string_view search) {
            if (search.empty()) {
                return true;
            }
            const std::string haystack = LowerCopy(
                info.presentation.displayName + " " +
                info.presentation.category + " " +
                info.typeName);
            return haystack.find(LowerCopy(search)) != std::string::npos;
        }

        void SelectObject(EditorContext& context, GameObject& object) {
            context.selection.selectedObject = &object;
            context.selection.selectedAsset = nullptr;
            context.selection.selectedAssetGuid.clear();
            context.selection.selectedAssetPath.clear();
        }

        void DrawStatusMessage(
            std::string_view message,
            bool error) {
            if (message.empty()) {
                return;
            }
            const std::string text(message);
            StatusBadge(
                text.c_str(),
                error
                    ? EditorStatusTone::Error
                    : EditorStatusTone::Ready);
        }
    }

    void SceneInspectorPanel::Draw(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        SceneObjectCommandService& commands,
        bool* open) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Inspector", open)) {
            ImGui::End();
            return;
        }
        DrawContents(scene, context, selectionSync, commands);
        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)commands;
        (void)open;
#endif
    }

    void SceneInspectorPanel::DrawContents(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
        GameObject* selected = context.selection.selectedObject;
        SceneObjectData* target = selected != nullptr
            ? selectionSync.FindDocumentObjectByRuntime(scene, selected)
            : nullptr;
        if (selected == nullptr || target == nullptr) {
            EmptyState(
                "No object selected",
                "Select one in the hierarchy or editor view.");
            return;
        }

        ImGui::PushID(static_cast<int>(target->id.value));
        PanelTitle(target->name.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s\nObject ID: %llu",
                target->name.c_str(),
                static_cast<unsigned long long>(target->id.value));
        }
        if (ImGui::BeginPopupContextItem("ObjectHeaderContext")) {
            DrawObjectContextMenu(
                scene,
                context,
                selectionSync,
                commands,
                *selected);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        const float menuWidth = ImGui::GetFrameHeight();
        ImGui::SetCursorPosX(
            (std::max)(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - menuWidth));
        if (IconButton(
                EditorGlyph::More,
                "ObjectActionsButton",
                EditorButtonTone::Quiet,
                ImVec2(menuWidth, menuWidth),
                "Object actions")) {
            ImGui::OpenPopup("ObjectActions");
        }
        if (ImGui::BeginPopup("ObjectActions")) {
            DrawObjectContextMenu(
                scene,
                context,
                selectionSync,
                commands,
                *selected);
            ImGui::EndPopup();
        }
        ImGui::PopID();

        DrawTransform(scene, context, *target);

        ImGui::Spacing();
        if (IconTextButton(
                EditorGlyph::Add,
                "Add Component",
                "AddComponent",
                EditorButtonTone::Neutral,
                ImVec2(-1.0f, 30.0f),
                "Add a component to this object")) {
            openComponentPicker_ = true;
        }

        DrawStatusMessage(
            commands.GetStatusMessage(),
            commands.IsStatusError());
        if (!context.componentAddStatusMessage.empty()) {
            DrawStatusMessage(
                context.componentAddStatusMessage,
                context.componentAddStatusIsError);
        }

        if (std::optional<SceneObjectAuthoringHistoryRequest>
                componentHistory = componentAuthoringSection_.Draw(
                    scene,
                    context,
                    selectionSync,
                    *target)) {
            historyRequest_ = std::move(componentHistory);
        }

#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)commands;
#endif
    }

    void SceneInspectorPanel::RequestRename(GameObject& object) {
        renameObjectId_ = object.GetDocumentId();
        std::snprintf(
            renameBuffer_,
            sizeof(renameBuffer_),
            "%s",
            object.GetName().c_str());
        openRenamePopup_ = true;
    }

    void SceneInspectorPanel::RequestFocus(SceneObjectId objectId) {
        if (objectId.value != 0u) {
            focusObjectRequest_ = objectId;
        }
    }

    void SceneInspectorPanel::DrawObjectContextMenu(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        SceneObjectCommandService& commands,
        GameObject& object) {
#if defined(HIKARI_WITH_EDITOR)
        if (context.selection.selectedObject != &object) {
            SelectObject(context, object);
        }
        SceneObjectData* target =
            selectionSync.FindDocumentObjectByRuntime(scene, &object);
        if (target == nullptr) {
            ImGui::TextDisabled("Object no longer exists");
            return;
        }

        const auto commandItem = [&](SceneObjectCommandId command) {
            const SceneObjectCommandPresentation& presentation =
                GetSceneObjectCommandPresentation(command);
            return ImGui::MenuItem(
                presentation.label,
                presentation.shortcut,
                false,
                commands.CanExecute(command, scene, context));
        };

        if (commandItem(SceneObjectCommandId::Rename)) {
            RequestRename(object);
        }
        if (commandItem(SceneObjectCommandId::Duplicate)) {
            commands.Execute(
                SceneObjectCommandId::Duplicate,
                scene,
                context,
                selectionSync);
            return;
        }
        if (commandItem(SceneObjectCommandId::Delete)) {
            commands.Execute(
                SceneObjectCommandId::Delete,
                scene,
                context,
                selectionSync);
            return;
        }

        ImGui::Separator();
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Focus", "F")) {
                focusObjectRequest_ = target->id;
            }
            if (ImGui::MenuItem("Add Component...")) {
                openComponentPicker_ = true;
            }
            if (ImGui::MenuItem("Collision...")) {
                collisionAuthoringDialog_.Open(target->id);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Prefab")) {
            if (ImGui::MenuItem("Save as Prefab...")) {
                prefabObjectId_ = target->id;
                std::snprintf(
                    prefabBuffer_,
                    sizeof(prefabBuffer_),
                    "%s",
                    target->name.c_str());
                openPrefabPopup_ = true;
            }
            const std::vector<std::string> prefabIds =
                commands.ListPrefabIds();
            if (!prefabIds.empty()) {
                ImGui::Separator();
                if (ImGui::BeginMenu("Instantiate")) {
                    for (const std::string& prefabId : prefabIds) {
                        if (ImGui::MenuItem(prefabId.c_str())) {
                            commands.Execute(
                                SceneObjectCommandId::InstantiatePrefab,
                                scene,
                                context,
                                selectionSync,
                                prefabId);
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenu();
        }

        if (HasDocumentComponent(*target, "CameraComponent") &&
            ImGui::BeginMenu("Camera")) {
            const bool isDefault =
                scene.GetSceneDocument().camera.defaultCameraObjectId ==
                    target->id;
            if (ImGui::MenuItem(
                    "Set as Game Default",
                    nullptr,
                    isDefault,
                    !isDefault)) {
                if (scene.SetGameDefaultCamera(target->id)) {
                    context.sceneDirty = true;
                }
            }
            const bool previewing =
                scene.IsEditorCameraPreviewActive() &&
                scene.GetEditorCameraPreviewObjectId() == target->id;
            if (ImGui::MenuItem(
                    previewing
                        ? "Exit Camera View"
                        : "View Through Camera")) {
                if (previewing) {
                    scene.EndEditorCameraPreview();
                } else {
                    (void)scene.BeginEditorCameraPreview(target->id);
                }
            }
            if (ImGui::MenuItem(
                    "Snap Camera to Current View",
                    nullptr,
                    false,
                    !target->parent.has_value())) {
                if (scene.SnapCameraObjectToEditorView(target->id)) {
                    context.sceneDirty = true;
                }
            }
            if (ImGui::MenuItem("Open Cinematics Workspace")) {
                openCinematicsWorkspaceCameraRequest_ = target->id;
            }
            ImGui::EndMenu();
        }
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)commands;
        (void)object;
#endif
    }

    void SceneInspectorPanel::DrawTransform(
        DocumentSceneBase& scene,
        EditorContext& context,
        SceneObjectData& target) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::CollapsingHeader(
                "Transform",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }

        const auto beginHistoryIfActivated = [&]() {
            if (ImGui::IsItemActivated() &&
                !pendingTransformHistory_) {
                pendingTransformHistory_ = PendingTransformHistory{
                    scene.GetSceneDocument().objects,
                    scene.GetSceneDocument().camera,
                    context.sceneDirty ||
                        scene.HasUnsavedSceneChanges()
                };
            }
        };
        const auto applyTransform = [&]() {
            if (context.selection.selectedObject == nullptr) {
                return;
            }
            Transform3D runtime =
                context.selection.selectedObject->GetTransform();
            runtime.position = target.transform.position;
            runtime.scale = target.transform.scale;
            runtime.rotation = MATH::Quat::FromEulerXYZ(
                target.transform.rotationEulerDeg.x * kDegreesToRadians,
                target.transform.rotationEulerDeg.y * kDegreesToRadians,
                target.transform.rotationEulerDeg.z * kDegreesToRadians);
            runtime.useExplicitMatrix = false;
            (void)context.selection.selectedObject->SetLocalTransform(runtime);
            context.sceneDirty = true;
        };
        const auto finishHistoryIfDeactivated = [&]() {
            if (!ImGui::IsItemDeactivatedAfterEdit() ||
                !pendingTransformHistory_) {
                return;
            }
            historyRequest_ = SceneObjectAuthoringHistoryRequest{
                "Edit Transform",
                std::move(pendingTransformHistory_->beforeObjects),
                scene.GetSceneDocument().objects,
                pendingTransformHistory_->beforeCamera,
                scene.GetSceneDocument().camera,
                pendingTransformHistory_->dirtyBefore
            };
            pendingTransformHistory_.reset();
        };

        {
            InspectorPropertyRow row("Position");
            float values[3]{
                target.transform.position.x,
                target.transform.position.y,
                target.transform.position.z
            };
            const bool changed = row.IsVisible() &&
                ImGui::DragFloat3("##Value", values, 0.05f);
            beginHistoryIfActivated();
            if (changed) {
                target.transform.position = {
                    values[0], values[1], values[2]
                };
                applyTransform();
            }
            finishHistoryIfDeactivated();
        }
        {
            InspectorPropertyRow row("Rotation");
            float values[3]{
                target.transform.rotationEulerDeg.x,
                target.transform.rotationEulerDeg.y,
                target.transform.rotationEulerDeg.z
            };
            const bool changed = row.IsVisible() &&
                ImGui::DragFloat3("##Value", values, 0.5f);
            beginHistoryIfActivated();
            if (changed) {
                target.transform.rotationEulerDeg = {
                    values[0], values[1], values[2]
                };
                applyTransform();
            }
            finishHistoryIfDeactivated();
        }
        {
            InspectorPropertyRow row("Scale");
            float values[3]{
                target.transform.scale.x,
                target.transform.scale.y,
                target.transform.scale.z
            };
            const bool changed = row.IsVisible() &&
                ImGui::DragFloat3("##Value", values, 0.01f);
            beginHistoryIfActivated();
            if (changed) {
                target.transform.scale = {
                    (std::max)(values[0], 0.0001f),
                    (std::max)(values[1], 0.0001f),
                    (std::max)(values[2], 0.0001f)
                };
                applyTransform();
            }
            finishHistoryIfDeactivated();
        }
#else
        (void)scene;
        (void)context;
        (void)target;
#endif
    }

    void SceneInspectorPanel::DrawAddComponentPopup(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
        if (openComponentPicker_) {
            ImGui::OpenPopup("AddComponentPopup");
            openComponentPicker_ = false;
        }
        if (!ImGui::BeginPopup("AddComponentPopup")) {
            return;
        }
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint(
            "##ComponentSearch",
            "Search components...",
            componentSearchBuffer_,
            sizeof(componentSearchBuffer_));
        ImGui::Separator();

        std::vector<const ComponentTypeInfo*> componentTypes =
            scene.GetComponentRegistry().GetTypeInfos();
        std::sort(
            componentTypes.begin(),
            componentTypes.end(),
            ComponentPresentationLess);
        std::string currentCategory{};
        for (const ComponentTypeInfo* typeInfo : componentTypes) {
            if (typeInfo == nullptr ||
                !MatchesComponent(*typeInfo, componentSearchBuffer_)) {
                continue;
            }
            if (currentCategory != typeInfo->presentation.category) {
                currentCategory = typeInfo->presentation.category;
                ImGui::SeparatorText(currentCategory.c_str());
            }
            if (ImGui::MenuItem(
                    typeInfo->presentation.displayName.c_str())) {
                commands.Execute(
                    SceneObjectCommandId::AddComponent,
                    scene,
                    context,
                    selectionSync,
                    typeInfo->typeName);
            }
            if (ImGui::IsItemHovered() &&
                !typeInfo->presentation.description.empty()) {
                ImGui::SetTooltip(
                    "%s",
                    typeInfo->presentation.description.c_str());
            }
        }
        ImGui::EndPopup();
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)commands;
#endif
    }

    void SceneInspectorPanel::DrawDeferredDialogs(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
        DrawAddComponentPopup(
            scene,
            context,
            selectionSync,
            commands);

        if (openRenamePopup_) {
            ImGui::OpenPopup("Rename Object");
            openRenamePopup_ = false;
        }
        if (ImGui::BeginPopupModal(
                "Rename Object",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(320.0f);
            const bool submit = ImGui::InputText(
                "##Name",
                renameBuffer_,
                sizeof(renameBuffer_),
                ImGuiInputTextFlags_EnterReturnsTrue);
            if (submit || ImGui::Button("Rename")) {
                if (context.selection.selectedObject != nullptr &&
                    context.selection.selectedObject->GetDocumentId() ==
                        renameObjectId_) {
                    commands.Execute(
                        SceneObjectCommandId::Rename,
                        scene,
                        context,
                        selectionSync,
                        renameBuffer_);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (openPrefabPopup_) {
            ImGui::OpenPopup("Save as Prefab");
            openPrefabPopup_ = false;
        }
        if (ImGui::BeginPopupModal(
                "Save as Prefab",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(320.0f);
            const bool submit = ImGui::InputText(
                "##PrefabName",
                prefabBuffer_,
                sizeof(prefabBuffer_),
                ImGuiInputTextFlags_EnterReturnsTrue);
            if (submit || ImGui::Button("Save")) {
                if (context.selection.selectedObject != nullptr &&
                    context.selection.selectedObject->GetDocumentId() ==
                        prefabObjectId_) {
                    commands.Execute(
                        SceneObjectCommandId::SaveAsPrefab,
                        scene,
                        context,
                        selectionSync,
                        prefabBuffer_);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        CollisionAuthoringRequest collisionRequest{};
        if (collisionAuthoringDialog_.Draw(collisionRequest)) {
            const bool dirtyBefore =
                context.sceneDirty || scene.HasUnsavedSceneChanges();
            const std::vector<SceneObjectData> beforeObjects =
                scene.GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera =
                scene.GetSceneDocument().camera;
            const CollisionAuthoringResult result =
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
                            CollisionAuthoringScope::SceneGeometry
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
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)commands;
#endif
    }

    std::optional<SceneObjectAuthoringHistoryRequest>
        SceneInspectorPanel::ConsumeHistoryRequest() {
        std::optional<SceneObjectAuthoringHistoryRequest> request =
            std::move(historyRequest_);
        historyRequest_.reset();
        return request;
    }

    std::optional<SceneObjectId>
        SceneInspectorPanel::ConsumeOpenCinematicsWorkspaceCameraRequest() {
        const std::optional<SceneObjectId> request =
            openCinematicsWorkspaceCameraRequest_;
        openCinematicsWorkspaceCameraRequest_.reset();
        return request;
    }

    std::optional<SceneObjectId>
        SceneInspectorPanel::ConsumeFocusObjectRequest() {
        const std::optional<SceneObjectId> request = focusObjectRequest_;
        focusObjectRequest_.reset();
        return request;
    }

} // namespace HIKARI::EDITOR
