#include "HIKARI_SandboxScene.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

#include "HIKARI_3D.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
#include "Render3D/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_SkyRenderer.h"
#include "Scene/Components/HIKARI_ModelComponent.h"

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

    void SandboxScene::OnEnter() {
        camera_.SetPerspective(60.0f * std::numbers::pi_v<float> / 180.0f, static_cast<float>(kScreenW) / static_cast<float>(kScreenH), 0.1f, 100.0f);
        debugCamera_.Reset({ 0.0f, 2.0f, -6.0f }, 0.0f, 0.0f);

        EnsureComponentRegistry();
        EnsureSceneRegistry();

        ReloadAssets();
        ReloadSceneDocument();
        RebuildRuntimeWorld();
    }

    void SandboxScene::OnExit() {
    }

    void SandboxScene::Update(float dt) {
        debugCamera_.Update(dt, camera_);
        world_.Update(dt);
    }

    void SandboxScene::Render() {
        RENDERER3D::Reset();
        MESHRENDERER::Reset();
        SKYRENDERER::Reset();

        RENDERER3D::DEBUG::Grid3D grid{};
        grid.halfCount = 10;
        grid.spacing = 1.0f;
        RENDERER3D::DEBUG::SubmitGrid3D(grid);

        RENDERER3D::DEBUG::Axis3D axis{};
        axis.length = 2.5f;
        RENDERER3D::DEBUG::SubmitAxis3D(axis);

        world_.Render();

        for (const auto& object : world_.GetObjects()) {
            if (const ModelComponent* model = object->GetComponent<ModelComponent>()) {
                if (!model->IsVisible()) {
                    continue;
                }

                const ModelAsset* asset = model->GetAsset();
                if (asset && asset->GetState() == ModelAsset::State::Loaded && asset->GetMesh() && asset->GetMesh()->IsValid()) {
                    MESHRENDERER::SubmitStaticMesh(*asset, object->Transform());
                }
                else {
                    RENDERER3D::WireCube cube{};
                    cube.transform = object->Transform();
                    cube.size = 1.0f;
                    cube.rgba = 0x66CCFFFF;
                    RENDERER3D::SubmitWireCube(cube);
                }
            }
        }

        SceneEnvironment activeEnvironment = environment_;
        activeEnvironment.directional.direction = MATH::Normalize(activeEnvironment.directional.direction);
        if (!environmentLightingEnabled_) {
            activeEnvironment.directional.intensity = 0.0f;
            activeEnvironment.ambient.intensity = 0.0f;
            activeEnvironment.specularIntensity = 0.0f;
            for (PointLight& pointLight : activeEnvironment.pointLights) {
                pointLight.intensity = 0.0f;
            }
        }

        SKYRENDERER::Render(camera_, activeEnvironment.sky, modelManager_, skyManager_);
        LIGHTDEBUGDRAW::SubmitDirectionalLightArrow(activeEnvironment.directional.direction, activeEnvironment);
        LIGHTDEBUGDRAW::SubmitPointLightDebug(activeEnvironment);
        MESHRENDERER::RenderAll(camera_, activeEnvironment);
        RENDERER3D::RenderAll(camera_, static_cast<float>(kScreenW), static_cast<float>(kScreenH));
    }

    void SandboxScene::RenderImGui() {
#if defined(_DEBUG)
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

    bool SandboxScene::ReloadAssets() {
        assetRegistry_.Clear();
        selection_.selectedAsset = nullptr;
        const bool okModels = assetJsonLoader_.LoadModelDescriptors("Data/assets_models.json", assetRegistry_);
        const bool okSkies = assetJsonLoader_.LoadSkyDescriptors("Data/assets_skies.json", assetRegistry_);
        const bool okTextures = assetJsonLoader_.LoadTextureDescriptors("Data/assets_textures.json", assetRegistry_);
        return okModels && okSkies && okTextures;
    }

    bool SandboxScene::ReloadSceneDocument() {
        const std::string* mappedPath = sceneRegistry_.FindPath(currentSceneId_);
        currentScenePath_ = mappedPath ? *mappedPath : "Data/scenes/scene_sandbox.json";

        if (!sceneSerializer_.LoadFromFile(currentScenePath_, sceneDocument_)) {
            return false;
        }

        environment_ = sceneDocument_.environment;
        nextSceneObjectId_ = 1;
        for (const SceneObjectData& object : sceneDocument_.objects) {
            nextSceneObjectId_ = (std::max)(nextSceneObjectId_, object.id.value + 1);
        }
        sceneNameEditBuffer_ = sceneDocument_.sceneName;
        saveAsNameBuffer_ = sceneDocument_.sceneName;
        sceneDirty_ = false;
        return true;
    }

    bool SandboxScene::RebuildRuntimeWorld() {
        const SceneObjectId previousSelectionId = selection_.selectedObject ? selection_.selectedObject->GetDocumentId() : SceneObjectId{};
        const SceneDependencySet deps = runtimeBuilder_.CollectDependencies(sceneDocument_);
        runtimeBuilder_.PreloadDependencies(deps, assetRegistry_, modelManager_, skyManager_);
        const bool built = runtimeBuilder_.BuildWorldFromDocument(sceneDocument_, world_, assetRegistry_, componentRegistry_, modelManager_, skyManager_);
        selection_.selectedObject = FindRuntimeObjectByDocumentId(previousSelectionId);
        selection_.selectedAsset = nullptr;
        environment_ = sceneDocument_.environment;
        environment_.directional.direction = MATH::Normalize(environment_.directional.direction);
        if (environment_.pointLights.empty()) {
            environment_.pointLights.push_back(PointLight{});
        }
        return built;
    }

    void SandboxScene::EnsureComponentRegistry() {
        if (componentRegistry_.Find("ModelComponent")) {
            return;
        }

        componentRegistry_.Register(ComponentTypeInfo{
            "ModelComponent",
            []() -> std::unique_ptr<IComponent> {
                return std::make_unique<ModelComponent>();
            }
            });
    }

    void SandboxScene::EnsureSceneRegistry() {
        sceneRegistry_.RegisterScene("Sandbox", "Data/scenes/scene_sandbox.json");
        sceneRegistry_.RegisterScene("Empty", "Data/scenes/scene_empty.json");

        std::error_code ec{};
        const std::filesystem::path sceneRoot{ "Data/scenes" };
        if (!std::filesystem::exists(sceneRoot, ec) || ec) {
            return;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(sceneRoot, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path& path = entry.path();
            if (path.extension() != ".json") {
                continue;
            }

            std::string stem = path.stem().string();
            if (stem == "scene_sandbox" || stem == "scene_empty") {
                continue;
            }
            if (stem.rfind("scene_", 0) == 0) {
                stem.erase(0, 6);
            }
            if (stem.empty()) {
                continue;
            }

            sceneRegistry_.RegisterScene(stem, path.generic_string());
        }
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
                currentScenePath_ = scenePath;
                currentSceneId_ = token;
                sceneRegistry_.RegisterScene(currentSceneId_, currentScenePath_);
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
            if (currentScenePath_.empty()) {
                saveSceneAsNewFile();
            } else if (sceneSerializer_.SaveToFile(currentScenePath_, sceneDocument_)) {
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
            currentSceneId_ = "Unsaved";
            currentScenePath_.clear();
            sceneNameEditBuffer_ = sceneDocument_.sceneName;
            saveAsNameBuffer_ = sceneDocument_.sceneName;
            nextSceneObjectId_ = 1;
            selection_.selectedObject = nullptr;
            selection_.selectedAsset = nullptr;
            MarkSceneDirty();
            RebuildRuntimeWorld();
        }

        std::vector<std::string> sceneIds = sceneRegistry_.GetSceneIds();
        std::sort(sceneIds.begin(), sceneIds.end());
        if (!sceneIds.empty()) {
            int currentSceneIndex = 0;
            for (int i = 0; i < static_cast<int>(sceneIds.size()); ++i) {
                if (sceneIds[i] == currentSceneId_) {
                    currentSceneIndex = i;
                    break;
                }
            }
            if (ImGui::BeginCombo("Scene Switcher", sceneIds[currentSceneIndex].c_str())) {
                for (int i = 0; i < static_cast<int>(sceneIds.size()); ++i) {
                    const bool selected = (i == currentSceneIndex);
                    if (ImGui::Selectable(sceneIds[i].c_str(), selected)) {
                        currentSceneId_ = sceneIds[i];
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
