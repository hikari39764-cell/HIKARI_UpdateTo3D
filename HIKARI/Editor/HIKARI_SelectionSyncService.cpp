#include "HIKARI_SelectionSyncService.h"

#include <algorithm>
#include <utility>

#include "HIKARI_EditorContext.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#undef max
#undef min

namespace HIKARI {

    SceneObjectData* SelectionSyncService::FindDocumentObjectById(DocumentSceneBase& scene, SceneObjectId id) const {
        for (SceneObjectData& object : scene.GetSceneDocument().objects) {
            if (object.id == id) {
                return &object;
            }
        }
        return nullptr;
    }

    SceneObjectData* SelectionSyncService::FindDocumentObjectByRuntime(DocumentSceneBase& scene, GameObject* runtimeObject) const {
        if (!runtimeObject) {
            return nullptr;
        }
        return FindDocumentObjectById(scene, runtimeObject->GetDocumentId());
    }

    GameObject* SelectionSyncService::FindRuntimeObjectByDocumentId(DocumentSceneBase& scene, SceneObjectId id) const {
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

    bool SelectionSyncService::SyncSelectedObjectBackToDocument(DocumentSceneBase& scene, EditorSelection& selection, bool& sceneDirty, uint64_t& nextSceneObjectId) const {
        SceneObjectData* documentObject = FindDocumentObjectByRuntime(scene, selection.selectedObject);
        if (!documentObject || !selection.selectedObject) {
            return false;
        }

        bool changed = false;
        bool requiresRebuild = false;

        const Transform3D& runtimeTransform = selection.selectedObject->Transform();
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

        const auto& runtimeComponents = selection.selectedObject->GetComponents();
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
            sceneDirty = true;
        }
        if (requiresRebuild) {
            RebuildRuntimeWorldWithSelectionSync(scene, selection, nextSceneObjectId);
        }

        return changed;
    }

    bool SelectionSyncService::RebuildRuntimeWorldWithSelectionSync(DocumentSceneBase& scene, EditorSelection& selection, uint64_t& nextSceneObjectId) const {
        const SceneObjectId previousSelectionId = selection.selectedObject ? selection.selectedObject->GetDocumentId() : SceneObjectId{};
        const bool built = scene.RebuildRuntimeWorld();
        selection.selectedObject = FindRuntimeObjectByDocumentId(scene, previousSelectionId);
        selection.selectedAsset = nullptr;
        SyncNextSceneObjectId(scene, nextSceneObjectId);
        return built;
    }

    void SelectionSyncService::SyncNextSceneObjectId(DocumentSceneBase& scene, uint64_t& nextSceneObjectId) const {
        uint64_t maxId = 0;
        for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
            maxId = std::max(maxId, object.id.value);
        }
        nextSceneObjectId = maxId + 1;
    }

} // namespace HIKARI
