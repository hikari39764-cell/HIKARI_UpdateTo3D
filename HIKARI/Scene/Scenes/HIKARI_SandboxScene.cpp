#include "HIKARI_SandboxScene.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Render3D/HIKARI_SkyRenderer.h"

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

    SandboxScene::SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId)
        : DocumentSceneBase(sceneCatalog, std::move(sceneId)) {
    }

    void SandboxScene::Update(float dt) {
        DocumentSceneBase::Update(dt);
    }

    void SandboxScene::RenderImGui() {
#if defined(_DEBUG)
        DocumentSceneBase::RenderImGui();

        if (!IsObjectAlive(world_, selection_.selectedObject)) {
            selection_.selectedObject = nullptr;
            selection_.selectedAsset = nullptr;
        }

        DrawDocumentToolbar();
        debugMenuBar_.Draw(debugWindowState_, debugCamera_, environmentLightingEnabled_);

        if (debugWindowState_.showHierarchy) {
            hierarchyPanel_.Draw(world_, selection_);
        }
        if (debugWindowState_.showInspector) {
            inspectorPanel_.Draw(selection_);
            SyncSelectedObjectBackToDocument();
        }
        if (debugWindowState_.showAssetBrowser) {
            assetBrowserPanel_.Draw(modelManager_, selection_);
        }
        if (debugWindowState_.showStats) {
            statsPanel_.Draw(GetSceneName(), world_, modelManager_, selection_, camera_);
        }
        if (debugWindowState_.showEnvironment) {
            environmentPanel_.Draw(environment_, &SKYRENDERER::GetDebugState());
            sceneDocument_.environment = environment_;
        }
        if (debugWindowState_.showDebugCamera) {
            debugCameraPanel_.Draw(debugCamera_);
        }
#endif
    }

    void SandboxScene::MarkSceneDirty() {
        sceneDirty_ = true;
    }

    bool SandboxScene::IsSceneDirty() const {
        return sceneDirty_;
    }

    SceneObjectData* SandboxScene::FindDocumentObjectById(SceneObjectId id) {
        for (SceneObjectData& object : sceneDocument_.objects) {
            if (object.id == id) {
                return &object;
            }
        }
        return nullptr;
    }

    SceneObjectData* SandboxScene::FindDocumentObjectByRuntime(GameObject* runtimeObject) {
        if (!runtimeObject) {
            return nullptr;
        }
        return FindDocumentObjectById(runtimeObject->GetDocumentId());
    }

    GameObject* SandboxScene::FindRuntimeObjectByDocumentId(SceneObjectId id) {
        if (id.value == 0) {
            return nullptr;
        }

        for (const auto& object : world_.GetObjects()) {
            if (object && object->GetDocumentId() == id) {
                return object.get();
            }
        }
        return nullptr;
    }

    void SandboxScene::SyncSelectedObjectBackToDocument() {
        SceneObjectData* documentObject = FindDocumentObjectByRuntime(selection_.selectedObject);
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
            RebuildRuntimeWorld();
        }
    }

    void SandboxScene::DrawDocumentToolbar() {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Document")) {
            ImGui::End();
            return;
        }

        char sceneNameBuffer[128]{};
        std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "%s", sceneNameEditBuffer_.c_str());
        if (ImGui::InputText("Scene Name", sceneNameBuffer, sizeof(sceneNameBuffer))) {
            sceneNameEditBuffer_ = sceneNameBuffer;
            if (sceneDocument_.sceneName != sceneNameEditBuffer_) {
                sceneDocument_.sceneName = sceneNameEditBuffer_;
                MarkSceneDirty();
            }
        }

        char saveAsBuffer[128]{};
        std::snprintf(saveAsBuffer, sizeof(saveAsBuffer), "%s", saveAsNameBuffer_.c_str());
        if (ImGui::InputText("Save As Name", saveAsBuffer, sizeof(saveAsBuffer))) {
            saveAsNameBuffer_ = saveAsBuffer;
        }

        auto saveSceneAsNewFile = [this]() {
            const std::string desiredName = saveAsNameBuffer_.empty() ? sceneDocument_.sceneName : saveAsNameBuffer_;
            const std::string token = SanitizeSceneToken(desiredName);
            const std::string scenePath = BuildScenePath(token);
            sceneDocument_.environment = environment_;
            if (sceneSerializer_.SaveToFile(scenePath, sceneDocument_)) {
                scenePath_ = scenePath;
                sceneId_ = token;
                sceneCatalog_.Register(SceneCatalogEntry{ sceneId_, "GameDocumentScene", scenePath_, true, sceneId_ });
                sceneDirty_ = false;
                if (!desiredName.empty()) {
                    sceneDocument_.sceneName = desiredName;
                    sceneNameEditBuffer_ = desiredName;
                } else {
                    sceneDocument_.sceneName = token;
                    sceneNameEditBuffer_ = token;
                }
                saveAsNameBuffer_ = sceneDocument_.sceneName;
            }
        };

        if (ImGui::Button("Reload Assets")) {
            ReloadAssets();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Scene")) {
            ReloadSceneDocument();
            RebuildRuntimeWorld();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Scene")) {
            sceneDocument_.environment = environment_;
            if (scenePath_.empty()) {
                saveSceneAsNewFile();
            } else if (sceneSerializer_.SaveToFile(scenePath_, sceneDocument_)) {
                sceneDirty_ = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As")) {
            saveSceneAsNewFile();
        }

        if (ImGui::Button("New Scene")) {
            sceneDocument_ = SceneDocument{};
            sceneDocument_.sceneName = "Untitled";
            sceneDocument_.environment = environment_;
            sceneId_ = "Unsaved";
            scenePath_.clear();
            sceneNameEditBuffer_ = sceneDocument_.sceneName;
            saveAsNameBuffer_ = sceneDocument_.sceneName;
            nextSceneObjectId_ = 1;
            selection_.selectedObject = nullptr;
            selection_.selectedAsset = nullptr;
            MarkSceneDirty();
            RebuildRuntimeWorld();
        }

        std::vector<std::string> sceneIds = sceneCatalog_.GetSceneIds();
        std::sort(sceneIds.begin(), sceneIds.end());
        if (!sceneIds.empty()) {
            int currentSceneIndex = 0;
            for (int i = 0; i < static_cast<int>(sceneIds.size()); ++i) {
                if (sceneIds[i] == sceneId_) {
                    currentSceneIndex = i;
                    break;
                }
            }
            if (ImGui::BeginCombo("Scene Switcher", sceneIds[currentSceneIndex].c_str())) {
                for (int i = 0; i < static_cast<int>(sceneIds.size()); ++i) {
                    const bool selected = (i == currentSceneIndex);
                    if (ImGui::Selectable(sceneIds[i].c_str(), selected)) {
                        sceneId_ = sceneIds[i];
                        ReloadSceneDocument();
                        RebuildRuntimeWorld();
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
            sceneDocument_.objects.push_back(newObject);
            MarkSceneDirty();
            RebuildRuntimeWorld();
        }

        if (selection_.selectedObject != nullptr) {
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected")) {
                SceneObjectData* target = FindDocumentObjectByRuntime(selection_.selectedObject);
                if (target != nullptr) {
                    const SceneObjectId targetId = target->id;
                    sceneDocument_.objects.erase(
                        std::remove_if(sceneDocument_.objects.begin(), sceneDocument_.objects.end(),
                            [targetId](const SceneObjectData& object) { return object.id == targetId; }),
                        sceneDocument_.objects.end());
                    selection_.selectedObject = nullptr;
                    selection_.selectedAsset = nullptr;
                    MarkSceneDirty();
                    RebuildRuntimeWorld();
                }
            }

            if (ImGui::Button("Duplicate Selected")) {
                if (SceneObjectData* target = FindDocumentObjectByRuntime(selection_.selectedObject)) {
                    SceneObjectData duplicate = *target;
                    duplicate.id = SceneObjectId{ nextSceneObjectId_++ };
                    duplicate.name = duplicate.name + "_Copy";
                    sceneDocument_.objects.push_back(std::move(duplicate));
                    MarkSceneDirty();
                    RebuildRuntimeWorld();
                }
            }

            if (SceneObjectData* target = FindDocumentObjectByRuntime(selection_.selectedObject)) {
                std::vector<std::string> componentTypes = componentRegistry_.GetTypeNames();
                std::sort(componentTypes.begin(), componentTypes.end());
                if (ImGui::BeginCombo("Add Component", "Select component type")) {
                    for (const std::string& typeName : componentTypes) {
                        if (ImGui::Selectable(typeName.c_str(), false)) {
                            target->components.push_back(SceneComponentData{ typeName, nlohmann::json::object() });
                            MarkSceneDirty();
                            RebuildRuntimeWorld();
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::SeparatorText("Document Components");
                bool needsRebuild = false;
                for (SceneComponentData& component : target->components) {
                    if (!ImGui::TreeNode(component.type.c_str())) {
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
                        std::vector<const AssetDescriptor*> modelAssets = assetRegistry_.CollectByType(AssetType::Model);
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
                        float rectX = rect.value("x", 20.0f);
                        float rectY = rect.value("y", 20.0f);
                        float rectW = rect.value("w", 200.0f);
                        float rectH = rect.value("h", 80.0f);
                        if (ImGui::DragFloat4("screenRect(x,y,w,h)", &rectX, 1.0f)) {
                            component.properties["screenRect"] = { {"x", rectX}, {"y", rectY}, {"w", rectW}, {"h", rectH} };
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
                }

                if (needsRebuild) {
                    RebuildRuntimeWorld();
                }
            }
        }

        ImGui::Text("Scene: %s%s", sceneDocument_.sceneName.c_str(), IsSceneDirty() ? "*" : "");
        ImGui::Text("Objects: %zu", sceneDocument_.objects.size());
        ImGui::End();
#endif
    }

} // namespace HIKARI
