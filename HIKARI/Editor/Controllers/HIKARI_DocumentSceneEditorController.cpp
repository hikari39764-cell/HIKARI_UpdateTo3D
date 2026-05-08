#include "Editor/HIKARI_DocumentSceneEditorController.h"

#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(_DEBUG)
#include "imgui.h"
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

        debugMenuBar_.Draw(context_.windows, scene.GetDebugCamera(), scene.GetEnvironmentLightingEnabled());

        if (context_.windows.authoring.showSceneWorkspace) {
            DrawSceneWorkspaceWindow(scene);
        }
        if (context_.windows.resources.showAssetBrowser) {
            assetBrowserPanel_.Draw(scene.GetModelManager(), context_.selection);
        }
        if (context_.windows.resources.showEnvironment) {
            environmentPanel_.Draw(scene.GetSceneEnvironment(), &SKYRENDERER::GetDebugState());
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        }
        if (context_.windows.runtime.showDebugWorkspace) {
            DrawDebugWorkspaceWindow(scene);
        }
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawSceneWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Workspace")) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("SceneWorkspaceTabs")) {
            if (ImGui::BeginTabItem("Document")) {
                documentToolbarController_.DrawContents(scene, context_, selectionSync_);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Authoring")) {
                sceneObjectAuthoringPanel_.DrawContents(scene, context_, selectionSync_);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Scene")) {
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

        if (ImGui::BeginTabBar("DebugWorkspaceTabs")) {
            if (ImGui::BeginTabItem("Overview")) {
                statsPanel_.DrawContents(scene.GetSceneName(), scene.GetWorld(), scene.GetModelManager(), context_.selection, scene.GetCamera());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Object")) {
                inspectorPanel_.DrawContents(context_.selection);
                selectionSync_.SyncSelectedObjectBackToDocument(scene, context_.selection, context_.sceneDirty, context_.nextSceneObjectId);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Viewport")) {
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
                if (ImGui::CollapsingHeader("Debug Camera")) {
                    debugCameraPanel_.DrawContents(scene.GetDebugCamera());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Time")) {
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
