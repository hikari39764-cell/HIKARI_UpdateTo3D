#include "Editor/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Style/HIKARI_EditorIconManager.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <json.hpp>

#if defined(_DEBUG)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI {

    namespace {
        bool IsObjectAlive(const World& world, const GameObject* object) {
            if (!object) {
                return false;
            }

            for (const auto& candidate : world.GetObjects()) {
                if (candidate.get() == object) {
                    return true;
                }
            }
            return false;
        }

        MATH::Vec3 ComputeDebugCameraForward(const DebugCameraController3D& camera) {
            const float cp = std::cos(camera.GetPitch());
            const float sp = std::sin(camera.GetPitch());
            const float cy = std::cos(camera.GetYaw());
            const float sy = std::sin(camera.GetYaw());
            return MATH::Normalize({ sy * cp, sp, cy * cp });
        }

#if defined(_DEBUG)
        void DrawEditorDockSpace(bool resetDefaultDockLayout) {
            ImGuiIO& io = ImGui::GetIO();
            if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
                return;
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImGuiID dockspaceId = ImGui::GetID("HIKARI_EditorDockSpace");
            const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

            static bool initializedDefaultDockLayout = false;
            if (!initializedDefaultDockLayout || resetDefaultDockLayout) {
                const bool needsDefaultLayout = ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
                initializedDefaultDockLayout = true;
                if (!needsDefaultLayout && !resetDefaultDockLayout) {
                    ImGui::DockSpaceOverViewport(dockspaceId, viewport, dockspaceFlags);
                    return;
                }

                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | dockspaceFlags);
                ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
                ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

                ImGuiID mainNode = dockspaceId;
                ImGuiID leftNode = 0;
                ImGuiID rightNode = 0;
                ImGuiID rightLowerNode = 0;
                ImGuiID rightEnvironmentNode = 0;
                ImGuiID rightResourceNode = 0;
                ImGuiID rightDebugNode = 0;
                ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Left, 0.23f, &leftNode, &mainNode);
                ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Right, 0.27f, &rightNode, &mainNode);
                ImGui::DockBuilderSplitNode(rightNode, ImGuiDir_Down, 0.58f, &rightLowerNode, &rightEnvironmentNode);
                ImGui::DockBuilderSplitNode(rightLowerNode, ImGuiDir_Down, 0.40f, &rightDebugNode, &rightResourceNode);

                ImGui::DockBuilderDockWindow("Game View", mainNode);
                ImGui::DockBuilderDockWindow("Scene Workspace", leftNode);
                ImGui::DockBuilderDockWindow("Environment", rightEnvironmentNode);
                ImGui::DockBuilderDockWindow("Resource Workspace", rightResourceNode);
                ImGui::DockBuilderDockWindow("Data Monitor", rightDebugNode);

                // Legacy standalone debug/editor windows are docked too if they are opened by older code or saved ImGui layouts.
                ImGui::DockBuilderDockWindow("Asset Browser", rightResourceNode);
                ImGui::DockBuilderDockWindow("Debug Camera", rightDebugNode);
                ImGui::DockBuilderDockWindow("Inspector", rightDebugNode);
                ImGui::DockBuilderDockWindow("Scene Document", leftNode);
                ImGui::DockBuilderDockWindow("Scene Hierarchy", leftNode);
                ImGui::DockBuilderDockWindow("Scene Object Authoring", leftNode);

                ImGui::DockBuilderFinish(dockspaceId);
            }

            ImGui::DockSpaceOverViewport(dockspaceId, viewport, dockspaceFlags);
        }
#endif
    }

    void DocumentSceneEditorController::Draw(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        if (!IsObjectAlive(scene.GetWorld(), context_.selection.selectedObject)) {
            context_.selection.selectedObject = nullptr;
            context_.selection.selectedAsset = nullptr;
        }

        documentToolbarController_.SyncDocumentMeta(scene, context_, selectionSync_);
        scene.SetUnsavedSceneChanges(context_.sceneDirty);
        if (context_.selection.selectedObject) {
            scene.SetSelectedGizmoObjectId(context_.selection.selectedObject->GetDocumentId());
        } else {
            scene.SetSelectedGizmoObjectId(SceneObjectId{});
        }
        scene.SetComponentGizmoState(context_.gizmos);
        scene.SetViewportOverlayState(context_.overlays);

        bool resetDockingLayoutRequested = false;
        debugMenuBar_.Draw(
            context_.windows,
            scene.GetDebugCamera(),
            scene.GetEnvironmentLightingEnabled(),
            resetDockingLayoutRequested);

        if (context_.windows.viewport.gameOnlyMode) {
            DrawGameViewportWindow(scene, true);
            DrawPendingSceneOpenModal(scene);
            return;
        }

#if defined(_DEBUG)
        DrawEditorDockSpace(resetDockingLayoutRequested);
#endif

        if (context_.windows.viewport.showGameView) {
            DrawGameViewportWindow(scene, false);
        } else {
            EDITOR::ClearGameViewportInputRect();
            POST::PostSystem::SetSceneCaptureSize(0, 0);
        }
        if (context_.windows.authoring.showSceneWorkspace) {
            DrawSceneWorkspaceWindow(scene);
        }
        if (context_.windows.resources.showAssetBrowser) {
            ProjectSettingsService projectSettings{};
            projectSettings.Load(scene.GetAssetDatabase().GetProjectRoot());
            const ResourceWorkspaceContext resourceContext{
                scene.GetCurrentSceneAssetGuid(),
                projectSettings.GetSettings().startupSceneGuid,
                context_.sceneDirty || scene.HasUnsavedSceneChanges()
            };
            resourceWorkspacePanel_.Draw(scene.GetAssetDatabase(), scene.GetSceneDocument(), context_.selection, resourceContext);

            const std::string saveSceneAsGuid = resourceWorkspacePanel_.ConsumeSaveSceneAsGuid();
            if (!saveSceneAsGuid.empty()) {
                if (scene.SaveCurrentSceneDocumentAs(AssetGuid{ saveSceneAsGuid })) {
                    context_.sceneDirty = false;
                    scene.SetUnsavedSceneChanges(false);
                    viewportDropMessage_ = "Scene saved to selected asset";
                } else {
                    viewportDropMessage_ = "Scene save target failed";
                }
            }

            const std::string activatedSceneGuid = resourceWorkspacePanel_.ConsumeActivatedSceneGuid();
            if (!activatedSceneGuid.empty()) {
                pendingSceneOpenGuid_ = AssetGuid{ activatedSceneGuid };
                if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
                    ImGui::OpenPopup("Unsaved Scene Changes");
                } else {
                    OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
                    pendingSceneOpenGuid_ = {};
                }
            }
        }
        if (context_.windows.resources.showEnvironment) {
            environmentPanel_.Draw(scene.GetSceneEnvironment(), &SKYRENDERER::GetDebugState(), &scene.GetAssetRegistry(), &scene.GetAssetDatabase());
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        }
        if (context_.windows.runtime.showDebugWorkspace) {
            DrawDebugWorkspaceWindow(scene);
        }
        DrawPendingSceneOpenModal(scene);
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawGameViewportWindow(DocumentSceneBase& scene, bool gameOnly) {
#if defined(_DEBUG)
        bool open = gameOnly ? true : context_.windows.viewport.showGameView;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse |
            (gameOnly ? (ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings) : 0);

        if (gameOnly) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (!ImGui::Begin("Game View", &open, flags)) {
            if (!gameOnly) {
                context_.windows.viewport.showGameView = open;
            }
            EDITOR::ClearGameViewportInputRect();
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        if (!gameOnly) {
            context_.windows.viewport.showGameView = open;
        }

        const float toolbarHeight = context_.windows.viewport.showViewportHud ? 38.0f : 0.0f;
        if (toolbarHeight > 0.0f) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.080f, 1.0f));
            if (ImGui::BeginChild("##GameViewToolbar", ImVec2(0.0f, toolbarHeight), false, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::SetCursorPosY(8.0f);
                ImGui::Dummy(ImVec2(8.0f, 0.0f));
                ImGui::SameLine();
                ImGui::TextUnformatted(gameOnly ? "Game Only" : "Game");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", scene.GetSceneId().c_str());
                ImGui::SameLine();

                ImGui::SetNextItemWidth(86.0f);
                const char* scaleLabel = "1.00x";
                if (context_.windows.viewport.gameViewResolutionScale <= 0.51f) {
                    scaleLabel = "0.50x";
                } else if (context_.windows.viewport.gameViewResolutionScale <= 0.76f) {
                    scaleLabel = "0.75x";
                }
                if (ImGui::BeginCombo("Scale", scaleLabel, ImGuiComboFlags_NoArrowButton)) {
                    if (ImGui::Selectable("0.50x", context_.windows.viewport.gameViewResolutionScale == 0.5f)) {
                        context_.windows.viewport.gameViewResolutionScale = 0.5f;
                    }
                    if (ImGui::Selectable("0.75x", context_.windows.viewport.gameViewResolutionScale == 0.75f)) {
                        context_.windows.viewport.gameViewResolutionScale = 0.75f;
                    }
                    if (ImGui::Selectable("1.00x", context_.windows.viewport.gameViewResolutionScale == 1.0f)) {
                        context_.windows.viewport.gameViewResolutionScale = 1.0f;
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                ImGui::Checkbox("Grid", &context_.overlays.showGrid);
                ImGui::SameLine();
                ImGui::Checkbox("Axis", &context_.overlays.showAxis);
                ImGui::SameLine();
                ImGui::Checkbox("Gizmos", &context_.gizmos.showComponentGizmos);
                ImGui::SameLine();
                ImGui::Checkbox("Game Only", &context_.windows.viewport.gameOnlyMode);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImVec2 imageSize = ImGui::GetContentRegionAvail();
        imageSize.x = (std::max)(imageSize.x, 1.0f);
        imageSize.y = (std::max)(imageSize.y, 1.0f);

        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        const bool gameViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        EDITOR::SetGameViewportInputRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y, gameViewFocused);

        const float resolutionScale = std::clamp(context_.windows.viewport.gameViewResolutionScale, 0.5f, 1.0f);
        context_.windows.viewport.gameViewResolutionScale = resolutionScale;
        const int captureWidth = (std::max)(16, static_cast<int>(imageSize.x * resolutionScale + 0.5f));
        const int captureHeight = (std::max)(16, static_cast<int>(imageSize.y * resolutionScale + 0.5f));
        POST::PostSystem::SetSceneCaptureSize(captureWidth, captureHeight);

        const bool ready = POST::PostSystem::EndSceneCaptureToEditorViewport();
        const D3D12_GPU_DESCRIPTOR_HANDLE viewportSrv = POST::PostSystem::GetEditorViewportSrv();
        if (ready && viewportSrv.ptr != 0) {
            const ImTextureID textureId = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(viewportSrv.ptr));
            ImGui::Image(textureId, imageSize);
            HandleGameViewportAssetDrop(scene);
        } else {
            const ImVec2 max{ imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(imageOrigin, max, IM_COL32(8, 10, 13, 255));
            drawList->AddRect(imageOrigin, max, IM_COL32(80, 108, 124, 160), 4.0f, 0, 1.0f);
            drawList->AddText(ImVec2(imageOrigin.x + 16.0f, imageOrigin.y + 16.0f), IM_COL32(190, 205, 215, 255), "Waiting for editor viewport texture");
            ImGui::Dummy(imageSize);
            HandleGameViewportAssetDrop(scene);
        }

        if (!viewportDropMessage_.empty()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 textPos{ imageOrigin.x + 14.0f, imageOrigin.y + imageSize.y - 28.0f };
            drawList->AddText(textPos, IM_COL32(210, 226, 236, 230), viewportDropMessage_.c_str());
        }

        ImGui::End();
        ImGui::PopStyleVar();
#else
        (void)scene;
        (void)gameOnly;
#endif
    }

    void DocumentSceneEditorController::HandleGameViewportAssetDrop(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        EDITOR::DroppedAssetPayload payload{};
        if (!EDITOR::AcceptAssetDrop(scene.GetAssetDatabase(), payload) || !payload.record) {
            return;
        }

        switch (payload.record->type) {
        case AssetType::Model: {
            const MATH::Vec3 forward = ComputeDebugCameraForward(scene.GetDebugCamera());
            EDITOR::CreateObjectRequest request{};
            request.name = payload.record->displayName.empty()
                ? payload.record->sourcePath.stem().string()
                : payload.record->displayName;
            request.position = scene.GetDebugCamera().GetPosition() + forward * 5.0f;

            GameObject* object = EDITOR::CreateModelObject(scene, payload.guid, request);
            context_.selection.selectedObject = object;
            context_.selection.selectedAsset = nullptr;
            context_.selection.selectedAssetGuid.clear();
            context_.selection.selectedAssetPath.clear();
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
            selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
            viewportDropMessage_ = object
                ? "Model object created: " + object->GetName()
                : "Model drop failed";
            break;
        }
        case AssetType::Scene:
            pendingSceneOpenGuid_ = payload.guid;
            if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
                ImGui::OpenPopup("Unsaved Scene Changes");
            } else {
                OpenSceneAssetFromEditor(scene, payload.guid);
            }
            break;
        case AssetType::VfxEffect:
            viewportDropMessage_ = "VFX drop target not implemented yet";
            break;
        case AssetType::Texture:
        case AssetType::Material:
            viewportDropMessage_ = "Texture/Material viewport drop is not implemented yet";
            break;
        default:
            viewportDropMessage_ = "This asset type cannot be dropped into Game View yet";
            break;
        }
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawPendingSceneOpenModal(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        bool openModal = true;
        if (!ImGui::BeginPopupModal("Unsaved Scene Changes", &openModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::TextUnformatted("Current scene has unsaved changes.");
        ImGui::TextDisabled("Save before opening the dropped Scene asset?");
        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(96.0f, 0.0f))) {
            if (scene.SaveCurrentSceneDocument()) {
                context_.sceneDirty = false;
                OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
                pendingSceneOpenGuid_ = {};
                ImGui::CloseCurrentPopup();
            } else {
                viewportDropMessage_ = "Save failed; scene was not opened";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(96.0f, 0.0f))) {
            context_.sceneDirty = false;
            scene.SetUnsavedSceneChanges(false);
            OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
            pendingSceneOpenGuid_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
            pendingSceneOpenGuid_ = {};
            viewportDropMessage_ = "Scene open cancelled";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
#else
        (void)scene;
#endif
    }

    bool DocumentSceneEditorController::OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            viewportDropMessage_ = "Invalid scene asset";
            return false;
        }

        const bool opened = scene.OpenSceneAssetNow(sceneGuid);
        if (!opened) {
            viewportDropMessage_ = "Scene asset open failed";
            return false;
        }

        context_.selection.selectedObject = nullptr;
        context_.selection.selectedAsset = nullptr;
        context_.selection.selectedAssetGuid = sceneGuid.value;
        context_.selection.selectedAssetPath.clear();
        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
        context_.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
        context_.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
        selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
        viewportDropMessage_ = "Scene opened: " + scene.GetSceneDocument().sceneName;
        return true;
    }

    void DocumentSceneEditorController::DrawSceneWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Workspace")) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("SceneWorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Objects")) {
                ImGui::SeparatorText("Objects");
                ImGui::TextDisabled("Current scene object list");
                hierarchyPanel_.DrawContents(scene.GetWorld(), context_.selection);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Create")) {
                ImGui::SeparatorText("Create Object");
                sceneObjectAuthoringPanel_.DrawContents(scene, context_, selectionSync_);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Scene Settings")) {
                ImGui::SeparatorText("Current Scene");
                SceneDocument& document = scene.GetSceneDocument();
                char sceneNameBuffer[128]{};
                std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "%s", document.sceneName.c_str());
                if (ImGui::InputText("Name", sceneNameBuffer, sizeof(sceneNameBuffer))) {
                    document.sceneName = sceneNameBuffer;
                    context_.sceneNameEditBuffer = document.sceneName;
                    context_.sceneDirty = true;
                    scene.SetUnsavedSceneChanges(true);
                }

                const AssetGuid& currentSceneGuid = scene.GetCurrentSceneAssetGuid();
                ImGui::Text("Asset GUID: %s", currentSceneGuid.IsValid() ? currentSceneGuid.value.c_str() : "<transient>");
                ImGui::TextWrapped("Path: %s", scene.GetScenePath().empty() ? "<not saved as Scene Asset>" : scene.GetScenePath().c_str());
                ImGui::Text("Dirty: %s", (context_.sceneDirty || scene.HasUnsavedSceneChanges()) ? "Yes" : "No");

                ImGui::Spacing();
                ImGui::TextDisabled("Scene file operations are handled in Resource Workspace.");
                ImGui::SeparatorText("Environment Summary");
                const SceneEnvironment& environment = scene.GetSceneEnvironment();
                ImGui::Text("Sky: %s", environment.sky.skyAsset.empty() ? "<none>" : environment.sky.skyAsset.c_str());
                ImGui::Text("Directional Light: %s / %.2f",
                    environment.directional.enabled ? "Enabled" : "Disabled",
                    environment.directional.intensity);
                ImGui::Text("Ambient: %.2f", environment.ambient.intensity);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Systems")) {
                ImGui::SeparatorText("Scene Systems");
                ImGui::TextDisabled("Runtime switching is reserved; this edits scene document data for now.");

                SceneDocument& document = scene.GetSceneDocument();
                if (document.systems.empty()) {
                    document.systems = {
                        SceneSystemData{ "TransformSystem", true, 0, nlohmann::json::object() },
                        SceneSystemData{ "ModelRenderSystem", true, 100, nlohmann::json::object() },
                        SceneSystemData{ "AnimationSystem", true, 150, nlohmann::json::object() },
                        SceneSystemData{ "VfxSystem", true, 200, nlohmann::json::object() },
                        SceneSystemData{ "PhysicsSystem", false, 300, nlohmann::json::object() },
                        SceneSystemData{ "ScriptSystem", false, 400, nlohmann::json::object() },
                    };
                }

                if (ImGui::BeginTable("SceneSystemsTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableSetupColumn("System");
                    ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                    ImGui::TableHeadersRow();

                    for (SceneSystemData& system : document.systems) {
                        ImGui::PushID(system.systemId.c_str());
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        if (ImGui::Checkbox("##enabled", &system.enabled)) {
                            context_.sceneDirty = true;
                            scene.SetUnsavedSceneChanges(true);
                        }

                        ImGui::TableSetColumnIndex(1);
                        EDITOR::EditorIconManager::DrawIcon(EDITOR::EditorIconKind::System, ImVec2(16.0f, 16.0f));
                        ImGui::SameLine();
                        ImGui::TextUnformatted(system.systemId.c_str());

                        ImGui::TableSetColumnIndex(2);
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::DragInt("##order", &system.executionOrder, 1.0f, -10000, 10000)) {
                            context_.sceneDirty = true;
                            scene.SetUnsavedSceneChanges(true);
                        }
                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawDebugWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        if (!ImGui::Begin("Data Monitor")) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("DebugWorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Stats")) {
                ImGui::SeparatorText("Frame Overview");
                statsPanel_.DrawContents(scene.GetSceneName(), scene.GetWorld(), scene.GetModelManager(), context_.selection, scene.GetCamera());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Inspector")) {
                ImGui::SeparatorText("Selection");
                inspectorPanel_.DrawContents(
                    context_.selection,
                    &scene.GetAssetRegistry(),
                    &scene.GetAssetDatabase(),
                    &scene.GetSceneCatalog());
                if (!ImGui::IsAnyItemActive()) {
                    selectionSync_.SyncSelectedObjectBackToDocument(scene, context_.selection, context_.sceneDirty, context_.nextSceneObjectId);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Viewport")) {
                ImGui::SeparatorText("Viewport Overlays");
                if (ImGui::CollapsingHeader("Viewport Overlays", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Grid", &context_.overlays.showGrid);
                    ImGui::Checkbox("Axis", &context_.overlays.showAxis);
                    ImGui::Separator();
                    ImGui::Checkbox("Component Gizmos", &context_.gizmos.showComponentGizmos);
                    ImGui::Checkbox("Only Selected Object", &context_.gizmos.showOnlySelectedObject);
                    ImGui::Checkbox("Trigger Volumes", &context_.gizmos.showTriggerVolumes);
                    ImGui::Checkbox("Spawn Points", &context_.gizmos.showSpawnPoints);
                    ImGui::Checkbox("Door Transitions", &context_.gizmos.showDoorTransitions);
                    ImGui::Checkbox("UI Screen Rects", &context_.gizmos.showUIScreenRects);
                }
                ImGui::SeparatorText("Camera");
                if (ImGui::CollapsingHeader("Debug Camera")) {
                    debugCameraPanel_.DrawContents(scene.GetDebugCamera());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Time")) {
                ImGui::SeparatorText("Timeline");
                timePanel_.DrawContents();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
#else
        (void)scene;
#endif
    }

} // namespace HIKARI
