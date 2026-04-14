#include "HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>
#include <utility>

#include "Assets/HIKARI_AssetTypes.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(_DEBUG)
#include "Render3D/HIKARI_SkyRenderer.h"
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
        if (!IsObjectAlive(scene.GetWorld(), selection_.selectedObject)) {
            selection_.selectedObject = nullptr;
            selection_.selectedAsset = nullptr;
        }

        SyncDocumentMeta(scene);
        DrawDocumentToolbar(scene);

        debugMenuBar_.Draw(debugWindowState_, scene.GetDebugCamera(), scene.GetEnvironmentLightingEnabled());

        if (debugWindowState_.showHierarchy) {
            hierarchyPanel_.Draw(scene.GetWorld(), selection_);
        }
        if (debugWindowState_.showInspector) {
            inspectorPanel_.Draw(selection_);
            SyncSelectedObjectBackToDocument(scene);
        }
        if (debugWindowState_.showAssetBrowser) {
            assetBrowserPanel_.Draw(scene.GetModelManager(), selection_);
        }
        if (debugWindowState_.showStats) {
            statsPanel_.Draw(scene.GetSceneName(), scene.GetWorld(), scene.GetModelManager(), selection_, scene.GetCamera());
        }
        if (debugWindowState_.showEnvironment) {
            environmentPanel_.Draw(scene.GetSceneEnvironment(), &SKYRENDERER::GetDebugState());
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        }
        if (debugWindowState_.showDebugCamera) {
            debugCameraPanel_.Draw(scene.GetDebugCamera());
        }
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::MarkSceneDirty() {
        sceneDirty_ = true;
    }

    bool DocumentSceneEditorController::IsSceneDirty() const {
        return sceneDirty_;
    }

    SceneObjectData* DocumentSceneEditorController::FindDocumentObjectById(DocumentSceneBase& scene, SceneObjectId id) {
        for (SceneObjectData& object : scene.GetSceneDocument().objects) {
            if (object.id == id) {
                return &object;
            }
        }
        return nullptr;
    }

    SceneObjectData* DocumentSceneEditorController::FindDocumentObjectByRuntime(DocumentSceneBase& scene, GameObject* runtimeObject) {
        if (!runtimeObject) {
            return nullptr;
        }
        return FindDocumentObjectById(scene, runtimeObject->GetDocumentId());
    }

    GameObject* DocumentSceneEditorController::FindRuntimeObjectByDocumentId(DocumentSceneBase& scene, SceneObjectId id) {
        if (id.value == 0) {
            return nullptr;
        }

        for (const auto& object : scene.GetWorld().GetObjects()) {
            if (object && object->GetDocumentId() == id) {
                return object.get();
            }
        }
        return nullptr;
    }

    void DocumentSceneEditorController::SyncSelectedObjectBackToDocument(DocumentSceneBase& scene) {
        SceneObjectData* documentObject = FindDocumentObjectByRuntime(scene, selection_.selectedObject);
        if (!documentObject || !selection_.selectedObject) {
            return;
        }

        const Transform3D& runtimeTransform = selection_.selectedObject->Transform();
        documentObject->transform.position = runtimeTransform.position;
        documentObject->transform.scale = runtimeTransform.scale;

        bool requiresRebuild = false;
        for (const auto& runtimeComponent : selection_.selectedObject->GetComponents()) {
            if (!runtimeComponent || runtimeComponent->GetTypeName() != "ModelComponent") {
                continue;
            }

            auto* runtimeModelComponent = dynamic_cast<const ModelComponent*>(runtimeComponent.get());
            if (!runtimeModelComponent) {
                continue;
            }

            for (SceneComponentData& componentData : documentObject->components) {
                if (componentData.type != "ModelComponent") {
                    continue;
                }

                const std::string runtimeAssetId = runtimeModelComponent->GetAssetId();
                const bool runtimeVisible = runtimeModelComponent->IsVisible();
                const std::string docAssetId = componentData.properties.value("assetId", std::string{});
                const bool docVisible = componentData.properties.value("visible", true);
                if (runtimeAssetId != docAssetId || runtimeVisible != docVisible) {
                    componentData.properties["assetId"] = runtimeAssetId;
                    componentData.properties["visible"] = runtimeVisible;
                    MarkSceneDirty();
                    requiresRebuild = true;
                }
                break;
            }
        }

        if (requiresRebuild) {
            RebuildRuntimeWorldWithSelectionSync(scene);
        }
    }

    bool DocumentSceneEditorController::RebuildRuntimeWorldWithSelectionSync(DocumentSceneBase& scene) {
        const SceneObjectId previousSelectionId = selection_.selectedObject ? selection_.selectedObject->GetDocumentId() : SceneObjectId{};
        const bool built = scene.RebuildRuntimeWorld();
        selection_.selectedObject = FindRuntimeObjectByDocumentId(scene, previousSelectionId);
        selection_.selectedAsset = nullptr;
        SyncNextSceneObjectId(scene);
        return built;
    }

    void DocumentSceneEditorController::SyncDocumentMeta(DocumentSceneBase& scene) {
        const SceneDocument& document = scene.GetSceneDocument();
        if (sceneNameEditBuffer_ != document.sceneName) {
            sceneNameEditBuffer_ = document.sceneName;
            saveAsNameBuffer_ = document.sceneName;
        }
        if (saveAsNameBuffer_.empty()) {
            saveAsNameBuffer_ = document.sceneName;
        }
        SyncNextSceneObjectId(scene);
    }

    void DocumentSceneEditorController::SyncNextSceneObjectId(DocumentSceneBase& scene) {
        uint64_t maxId = 0;
        for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
            maxId = std::max(maxId, object.id.value);
        }
        nextSceneObjectId_ = maxId + 1;
    }

    void DocumentSceneEditorController::DrawDocumentToolbar(DocumentSceneBase& scene) {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Document")) {
            ImGui::End();
            return;
        }

        char sceneNameBuffer[128]{};
        std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "%s", sceneNameEditBuffer_.c_str());
        if (ImGui::InputText("Scene Name", sceneNameBuffer, sizeof(sceneNameBuffer))) {
            sceneNameEditBuffer_ = sceneNameBuffer;
            if (scene.GetSceneDocument().sceneName != sceneNameEditBuffer_) {
                scene.GetSceneDocument().sceneName = sceneNameEditBuffer_;
                MarkSceneDirty();
            }
        }

        char saveAsBuffer[128]{};
        std::snprintf(saveAsBuffer, sizeof(saveAsBuffer), "%s", saveAsNameBuffer_.c_str());
        if (ImGui::InputText("Save As Name", saveAsBuffer, sizeof(saveAsBuffer))) {
            saveAsNameBuffer_ = saveAsBuffer;
        }

        auto saveSceneAsNewFile = [this, &scene]() {
            SceneDocument& sceneDocument = scene.GetSceneDocument();
            const std::string desiredName = saveAsNameBuffer_.empty() ? sceneDocument.sceneName : saveAsNameBuffer_;
            const std::string token = SanitizeSceneToken(desiredName);
            const std::string scenePath = BuildScenePath(token);
            sceneDocument.environment = scene.GetSceneEnvironment();
            SceneSerializer serializer{};
            if (serializer.SaveToFile(scenePath, sceneDocument)) {
                scene.SetScenePath(scenePath);
                scene.SetSceneId(token);
                scene.GetSceneCatalog().Register(SceneCatalogEntry{ scene.GetSceneId(), "GameDocumentScene", scene.GetScenePath(), true, scene.GetSceneId() });
                sceneDirty_ = false;
                if (!desiredName.empty()) {
                    sceneDocument.sceneName = desiredName;
                    sceneNameEditBuffer_ = desiredName;
                } else {
                    sceneDocument.sceneName = token;
                    sceneNameEditBuffer_ = token;
                }
                saveAsNameBuffer_ = sceneDocument.sceneName;
            }
        };

        if (ImGui::Button("Reload Assets")) {
            scene.ReloadAssets();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Scene")) {
            scene.ReloadSceneDocument();
            sceneDirty_ = false;
            RebuildRuntimeWorldWithSelectionSync(scene);
            sceneNameEditBuffer_ = scene.GetSceneDocument().sceneName;
            saveAsNameBuffer_ = scene.GetSceneDocument().sceneName;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Scene")) {
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
            SceneSerializer serializer{};
            if (scene.GetScenePath().empty()) {
                saveSceneAsNewFile();
            } else if (serializer.SaveToFile(scene.GetScenePath(), scene.GetSceneDocument())) {
                sceneDirty_ = false;
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
            sceneNameEditBuffer_ = scene.GetSceneDocument().sceneName;
            saveAsNameBuffer_ = scene.GetSceneDocument().sceneName;
            nextSceneObjectId_ = 1;
            selection_.selectedObject = nullptr;
            selection_.selectedAsset = nullptr;
            MarkSceneDirty();
            RebuildRuntimeWorldWithSelectionSync(scene);
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
                        sceneDirty_ = false;
                        RebuildRuntimeWorldWithSelectionSync(scene);
                        sceneNameEditBuffer_ = scene.GetSceneDocument().sceneName;
                        saveAsNameBuffer_ = scene.GetSceneDocument().sceneName;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (ImGui::Button("Create Object")) {
            SceneObjectData newObject{};
            newObject.id = SceneObjectId{ nextSceneObjectId_++ };
            newObject.name = "GameObject_" + std::to_string(newObject.id.value);
            scene.GetSceneDocument().objects.push_back(newObject);
            MarkSceneDirty();
            RebuildRuntimeWorldWithSelectionSync(scene);
        }

        if (selection_.selectedObject != nullptr) {
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected")) {
                SceneObjectData* target = FindDocumentObjectByRuntime(scene, selection_.selectedObject);
                if (target != nullptr) {
                    const SceneObjectId targetId = target->id;
                    auto& objects = scene.GetSceneDocument().objects;
                    objects.erase(
                        std::remove_if(objects.begin(), objects.end(),
                            [targetId](const SceneObjectData& object) { return object.id == targetId; }),
                        objects.end());
                    selection_.selectedObject = nullptr;
                    selection_.selectedAsset = nullptr;
                    MarkSceneDirty();
                    RebuildRuntimeWorldWithSelectionSync(scene);
                }
            }

            if (ImGui::Button("Duplicate Selected")) {
                if (SceneObjectData* target = FindDocumentObjectByRuntime(scene, selection_.selectedObject)) {
                    SceneObjectData duplicate = *target;
                    duplicate.id = SceneObjectId{ nextSceneObjectId_++ };
                    duplicate.name = duplicate.name + "_Copy";
                    scene.GetSceneDocument().objects.push_back(std::move(duplicate));
                    MarkSceneDirty();
                    RebuildRuntimeWorldWithSelectionSync(scene);
                }
            }

            if (SceneObjectData* target = FindDocumentObjectByRuntime(scene, selection_.selectedObject)) {
                std::vector<std::string> componentTypes = scene.GetComponentRegistry().GetTypeNames();
                std::sort(componentTypes.begin(), componentTypes.end());
                if (ImGui::BeginCombo("Add Component", "Select component type")) {
                    for (const std::string& typeName : componentTypes) {
                        if (ImGui::Selectable(typeName.c_str(), false)) {
                            target->components.push_back(SceneComponentData{ typeName, nlohmann::json::object() });
                            MarkSceneDirty();
                            RebuildRuntimeWorldWithSelectionSync(scene);
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::SeparatorText("Document Components");
                bool needsRebuild = false;
                for (size_t componentIndex = 0; componentIndex < target->components.size(); ++componentIndex) {
                    SceneComponentData& component = target->components[componentIndex];
                    ImGui::PushID(static_cast<int>(componentIndex));
                    if (!ImGui::TreeNode(component.type.c_str())) {
                        ImGui::PopID();
                        continue;
                    }

                    if (component.type == "ModelComponent") {
                        bool visible = component.properties.value("visible", true);
                        if (ImGui::Checkbox("Visible", &visible)) {
                            component.properties["visible"] = visible;
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        std::string assetId = component.properties.value("assetId", std::string{});
                        std::vector<const AssetDescriptor*> modelAssets = scene.GetAssetRegistry().CollectByType(AssetType::Model);
                        if (ImGui::BeginCombo("Model Asset", assetId.empty() ? "<none>" : assetId.c_str())) {
                            for (const AssetDescriptor* descriptor : modelAssets) {
                                if (!descriptor) {
                                    continue;
                                }
                                const bool selected = (assetId == descriptor->id.value);
                                if (ImGui::Selectable(descriptor->id.value.c_str(), selected)) {
                                    component.properties["assetId"] = descriptor->id.value;
                                    MarkSceneDirty();
                                    needsRebuild = true;
                                }
                                if (selected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    } else if (component.type == "UIButtonSceneTransitionComponent") {
                        bool enabled = component.properties.value("enabled", true);
                        if (ImGui::Checkbox("Enabled", &enabled)) {
                            component.properties["enabled"] = enabled;
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        auto rect = component.properties.value("screenRect", nlohmann::json::object());
                        float rectValues[4]{
                            rect.value("x", 20.0f),
                            rect.value("y", 20.0f),
                            rect.value("w", 200.0f),
                            rect.value("h", 80.0f)
                        };
                        if (ImGui::DragFloat4("screenRect(x,y,w,h)", rectValues, 1.0f)) {
                            component.properties["screenRect"] = {{"x", rectValues[0]}, {"y", rectValues[1]}, {"w", rectValues[2]}, {"h", rectValues[3]}};
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        std::string targetSceneId = component.properties.value("targetSceneId", std::string("Title"));
                        char targetSceneBuffer[128]{};
                        std::snprintf(targetSceneBuffer, sizeof(targetSceneBuffer), "%s", targetSceneId.c_str());
                        if (ImGui::InputText("Target Scene ID", targetSceneBuffer, sizeof(targetSceneBuffer))) {
                            component.properties["targetSceneId"] = std::string(targetSceneBuffer);
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        std::string spawnPointId = component.properties.value("targetSpawnPointId", std::string{});
                        char spawnBuffer[128]{};
                        std::snprintf(spawnBuffer, sizeof(spawnBuffer), "%s", spawnPointId.c_str());
                        if (ImGui::InputText("Target Spawn Point", spawnBuffer, sizeof(spawnBuffer))) {
                            component.properties["targetSpawnPointId"] = std::string(spawnBuffer);
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        std::string transitionProfile = component.properties.value("transitionProfileId", std::string("DefaultFade"));
                        char transitionBuffer[128]{};
                        std::snprintf(transitionBuffer, sizeof(transitionBuffer), "%s", transitionProfile.c_str());
                        if (ImGui::InputText("Transition Profile", transitionBuffer, sizeof(transitionBuffer))) {
                            component.properties["transitionProfileId"] = std::string(transitionBuffer);
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        bool useTransition = component.properties.value("useTransition", true);
                        if (ImGui::Checkbox("Use Transition", &useTransition)) {
                            component.properties["useTransition"] = useTransition;
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        bool requireLeftClick = component.properties.value("requireLeftClick", true);
                        if (ImGui::Checkbox("Require Left Click", &requireLeftClick)) {
                            component.properties["requireLeftClick"] = requireLeftClick;
                            MarkSceneDirty();
                            needsRebuild = true;
                        }

                        bool debugDrawRect = component.properties.value("debugDrawRect", true);
                        if (ImGui::Checkbox("Debug Draw Rect", &debugDrawRect)) {
                            component.properties["debugDrawRect"] = debugDrawRect;
                            MarkSceneDirty();
                            needsRebuild = true;
                        }
                    }
                    ImGui::TreePop();
                    ImGui::PopID();
                }

                if (needsRebuild) {
                    RebuildRuntimeWorldWithSelectionSync(scene);
                }
            }
        }

        ImGui::Text("Scene: %s%s", scene.GetSceneDocument().sceneName.c_str(), IsSceneDirty() ? "*" : "");
        ImGui::Text("Objects: %zu", scene.GetSceneDocument().objects.size());
        ImGui::End();
#else
        (void)scene;
#endif
    }

} // namespace HIKARI
