#include "HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>
#include <utility>

#include "Scene/HIKARI_GameObject.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"

#undef max
#undef min

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

        bool changed = false;
        bool requiresRebuild = false;

        const Transform3D& runtimeTransform = selection_.selectedObject->Transform();
        if (documentObject->transform.position.x != runtimeTransform.position.x
            || documentObject->transform.position.y != runtimeTransform.position.y
            || documentObject->transform.position.z != runtimeTransform.position.z) {
            documentObject->transform.position = runtimeTransform.position;
            changed = true;
        }
        if (documentObject->transform.scale.x != runtimeTransform.scale.x
            || documentObject->transform.scale.y != runtimeTransform.scale.y
            || documentObject->transform.scale.z != runtimeTransform.scale.z) {
            documentObject->transform.scale = runtimeTransform.scale;
            changed = true;
        }

        const auto& runtimeComponents = selection_.selectedObject->GetComponents();
        const size_t count = (std::min)(runtimeComponents.size(), documentObject->components.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& runtimeComponent = runtimeComponents[i];
            SceneComponentData& componentData = documentObject->components[i];
            if (!runtimeComponent || std::string(runtimeComponent->GetTypeName()) != componentData.type) {
                continue;
            }

            nlohmann::json serialized = nlohmann::json::object();
            runtimeComponent->Serialize(serialized);
            if (serialized == componentData.properties) {
                continue;
            }

            componentData.properties = std::move(serialized);
            changed = true;
            requiresRebuild = true;
        }

        if (changed) {
            MarkSceneDirty();
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
                char prefabNameBuffer[128]{};
                std::snprintf(prefabNameBuffer, sizeof(prefabNameBuffer), "%s", prefabNameBuffer_.c_str());
                if (ImGui::InputText("Prefab ID", prefabNameBuffer, sizeof(prefabNameBuffer))) {
                    prefabNameBuffer_ = prefabNameBuffer;
                }

                if (ImGui::Button("Save Selected As Prefab")) {
                    const std::string prefabId = SanitizeSceneToken(prefabNameBuffer_.empty() ? target->name : prefabNameBuffer_);
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
                    if (prefabNameBuffer_.empty()) {
                        prefabNameBuffer_ = prefabIds.front();
                    }

                    if (ImGui::BeginCombo("Create From Prefab", prefabNameBuffer_.c_str())) {
                        for (const std::string& prefabId : prefabIds) {
                            const bool selected = (prefabNameBuffer_ == prefabId);
                            if (ImGui::Selectable(prefabId.c_str(), selected)) {
                                prefabNameBuffer_ = prefabId;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::Button("Instantiate Prefab")) {
                        PrefabDocument prefab{};
                        if (prefabRegistry_.Load(prefabNameBuffer_, prefab, prefabSerializer_)) {
                            SceneObjectData instance = prefab.rootObject;
                            instance.id = SceneObjectId{ nextSceneObjectId_++ };
                            instance.parent.reset();
                            instance.sourcePrefabId = prefabNameBuffer_;
                            if (instance.name.empty()) {
                                instance.name = prefab.prefabName;
                            }
                            scene.GetSceneDocument().objects.push_back(std::move(instance));
                            MarkSceneDirty();
                            RebuildRuntimeWorldWithSelectionSync(scene);
                        }
                    }
                }

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
                const InspectorContext context{
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

                    if (componentDocumentEditor_.DrawComponent(scene.GetComponentRegistry(), component, componentInspectorBuilder_, context)) {
                        MarkSceneDirty();
                        needsRebuild = true;
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
