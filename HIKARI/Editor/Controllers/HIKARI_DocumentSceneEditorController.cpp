#include "Editor/HIKARI_DocumentSceneEditorController.h"

#include "Editor/HIKARI_EditorViewportInput.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <cstdint>

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
            resourceWorkspacePanel_.Draw(scene.GetAssetDatabase(), scene.GetSceneDocument(), context_.selection);
        }
        if (context_.windows.resources.showEnvironment) {
            environmentPanel_.Draw(scene.GetSceneEnvironment(), &SKYRENDERER::GetDebugState(), &scene.GetAssetRegistry(), &scene.GetAssetDatabase());
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        }
        if (context_.windows.runtime.showDebugWorkspace) {
            DrawDebugWorkspaceWindow(scene);
        }
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
        } else {
            const ImVec2 max{ imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(imageOrigin, max, IM_COL32(8, 10, 13, 255));
            drawList->AddRect(imageOrigin, max, IM_COL32(80, 108, 124, 160), 4.0f, 0, 1.0f);
            drawList->AddText(ImVec2(imageOrigin.x + 16.0f, imageOrigin.y + 16.0f), IM_COL32(190, 205, 215, 255), "Waiting for editor viewport texture");
            ImGui::Dummy(imageSize);
        }

        ImGui::End();
        ImGui::PopStyleVar();
#else
        (void)scene;
        (void)gameOnly;
#endif
    }

    void DocumentSceneEditorController::DrawSceneWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Workspace")) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("SceneWorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Document")) {
                ImGui::SeparatorText("Scene Document");
                documentToolbarController_.DrawContents(scene, context_, selectionSync_);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Create")) {
                ImGui::SeparatorText("Object Authoring");
                sceneObjectAuthoringPanel_.DrawContents(scene, context_, selectionSync_);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Objects")) {
                ImGui::SeparatorText("Hierarchy");
                hierarchyPanel_.DrawContents(scene.GetWorld(), context_.selection);
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
