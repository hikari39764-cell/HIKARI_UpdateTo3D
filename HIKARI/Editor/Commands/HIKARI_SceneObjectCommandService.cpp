#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
    namespace {
        constexpr SceneObjectCommandPresentation kPresentations[] = {
            { "Rename", "F2" },
            { "Duplicate", "Ctrl+D" },
            { "Delete", "Delete" },
            { "Save as Prefab...", "" },
            { "Instantiate Prefab", "" },
            { "Add Component...", "" },
        };

        std::string SanitizePrefabToken(std::string_view raw) {
            std::string sanitized{};
            sanitized.reserve(raw.size());
            for (char ch : raw) {
                const unsigned char value =
                    static_cast<unsigned char>(ch);
                if (std::isalnum(value) != 0 || ch == '_' || ch == '-') {
                    sanitized.push_back(ch);
                } else if (!std::isspace(value)) {
                    sanitized.push_back('_');
                }
            }
            return sanitized.empty() ? "NewPrefab" : sanitized;
        }

        SceneObjectData* FindSelectedDocumentObject(
            DocumentSceneBase& scene,
            const EditorContext& context,
            const SelectionSyncService& selectionSync) {

            if (context.selection.selectedObject == nullptr) {
                return nullptr;
            }
            return selectionSync.FindDocumentObjectByRuntime(
                scene,
                context.selection.selectedObject);
        }

        std::unordered_set<uint64_t> CollectObjectSubtreeIds(
            const SceneDocument& document,
            SceneObjectId rootId) {

            std::unordered_set<uint64_t> ids{ rootId.value };
            bool changed = true;
            while (changed) {
                changed = false;
                for (const SceneObjectData& object : document.objects) {
                    if (!object.parent || ids.contains(object.id.value)) {
                        continue;
                    }
                    if (ids.contains(object.parent->value)) {
                        ids.insert(object.id.value);
                        changed = true;
                    }
                }
            }
            return ids;
        }

        SceneObjectAuthoringHistoryRequest MakeHistory(
            std::string label,
            std::vector<SceneObjectData> beforeObjects,
            SceneCameraSettings beforeCamera,
            bool dirtyBefore,
            const DocumentSceneBase& scene) {

            return SceneObjectAuthoringHistoryRequest{
                std::move(label),
                std::move(beforeObjects),
                scene.GetSceneDocument().objects,
                std::move(beforeCamera),
                scene.GetSceneDocument().camera,
                dirtyBefore
            };
        }
    }

    const SceneObjectCommandPresentation& GetSceneObjectCommandPresentation(
        SceneObjectCommandId command) noexcept {

        return kPresentations[static_cast<size_t>(command)];
    }

    bool SceneObjectCommandService::CanExecute(
        SceneObjectCommandId command,
        const DocumentSceneBase&,
        const EditorContext& context,
        std::string_view argument) const {

        switch (command) {
        case SceneObjectCommandId::InstantiatePrefab:
            return !argument.empty();
        case SceneObjectCommandId::Rename:
        case SceneObjectCommandId::Duplicate:
        case SceneObjectCommandId::Delete:
        case SceneObjectCommandId::SaveAsPrefab:
        case SceneObjectCommandId::AddComponent:
            return context.selection.selectedObject != nullptr &&
                (command != SceneObjectCommandId::AddComponent ||
                    !argument.empty());
        }
        return false;
    }

    bool SceneObjectCommandService::Execute(
        SceneObjectCommandId command,
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        std::string_view argument) {

        if (!CanExecute(command, scene, context, argument)) {
            SetStatus("Command is unavailable for the current selection.", true);
            return false;
        }

        SceneObjectData* target = FindSelectedDocumentObject(
            scene,
            context,
            selectionSync);
        if (command != SceneObjectCommandId::InstantiatePrefab &&
            target == nullptr) {
            SetStatus("The selected object no longer exists.", true);
            return false;
        }

        const bool dirtyBefore =
            context.sceneDirty || scene.HasUnsavedSceneChanges();
        std::vector<SceneObjectData> beforeObjects =
            scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene.GetSceneDocument().camera;

        switch (command) {
        case SceneObjectCommandId::Rename: {
            const std::string newName(argument);
            if (newName.empty() || target->name == newName) {
                return false;
            }
            target->name = newName;
            context.selection.selectedObject->SetName(newName);
            context.sceneDirty = true;
            historyRequest_ = MakeHistory(
                "Rename Object",
                std::move(beforeObjects),
                beforeCamera,
                dirtyBefore,
                scene);
            SetStatus("Renamed object to " + newName + ".", false);
            return true;
        }
        case SceneObjectCommandId::Duplicate: {
            const SceneObjectId sourceRootId = target->id;
            const std::unordered_set<uint64_t> subtreeIds =
                CollectObjectSubtreeIds(
                    scene.GetSceneDocument(),
                    sourceRootId);
            std::unordered_map<uint64_t, SceneObjectId> remappedIds{};
            remappedIds.reserve(subtreeIds.size());
            for (const SceneObjectData& object : beforeObjects) {
                if (subtreeIds.contains(object.id.value)) {
                    remappedIds.emplace(
                        object.id.value,
                        SceneObjectId{ context.nextSceneObjectId++ });
                }
            }
            const SceneObjectId duplicateRootId =
                remappedIds.at(sourceRootId.value);
            auto& objects = scene.GetSceneDocument().objects;
            objects.reserve(objects.size() + subtreeIds.size());
            for (const SceneObjectData& source : beforeObjects) {
                if (!subtreeIds.contains(source.id.value)) {
                    continue;
                }
                SceneObjectData duplicate = source;
                duplicate.id = remappedIds.at(source.id.value);
                if (source.id == sourceRootId) {
                    duplicate.name += "_Copy";
                }
                if (duplicate.parent &&
                    remappedIds.contains(duplicate.parent->value)) {
                    duplicate.parent = remappedIds.at(
                        duplicate.parent->value);
                }
                objects.push_back(std::move(duplicate));
            }
            context.sceneDirty = true;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(
                scene,
                context.selection,
                context.nextSceneObjectId);
            context.selection.selectedObject =
                selectionSync.FindRuntimeObjectByDocumentId(
                    scene,
                    duplicateRootId);
            historyRequest_ = MakeHistory(
                "Duplicate Object",
                std::move(beforeObjects),
                beforeCamera,
                dirtyBefore,
                scene);
            SetStatus("Duplicated selected object hierarchy.", false);
            return true;
        }
        case SceneObjectCommandId::Delete: {
            const std::unordered_set<uint64_t> deletedIds =
                CollectObjectSubtreeIds(
                    scene.GetSceneDocument(),
                    target->id);
            const std::optional<SceneObjectId>& defaultCamera =
                scene.GetSceneDocument().camera.defaultCameraObjectId;
            if (defaultCamera &&
                deletedIds.contains(defaultCamera->value)) {
                scene.ClearGameDefaultCamera();
            }
            if (scene.IsEditorCameraPreviewActive() &&
                deletedIds.contains(
                    scene.GetEditorCameraPreviewObjectId().value)) {
                scene.EndEditorCameraPreview();
            }
            auto& objects = scene.GetSceneDocument().objects;
            std::erase_if(
                objects,
                [&deletedIds](const SceneObjectData& object) {
                    return deletedIds.contains(object.id.value);
                });
            context.selection.selectedObject = nullptr;
            context.selection.selectedAsset = nullptr;
            context.selection.selectedAssetGuid.clear();
            context.selection.selectedAssetPath.clear();
            context.sceneDirty = true;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(
                scene,
                context.selection,
                context.nextSceneObjectId);
            historyRequest_ = MakeHistory(
                "Delete Object",
                std::move(beforeObjects),
                beforeCamera,
                dirtyBefore,
                scene);
            SetStatus("Deleted selected object hierarchy.", false);
            return true;
        }
        case SceneObjectCommandId::SaveAsPrefab: {
            const std::string prefabId = SanitizePrefabToken(
                argument.empty() ? target->name : argument);
            PrefabDocument prefab{};
            prefab.prefabName = prefabId;
            prefab.rootObject = *target;
            prefab.rootObject.parent.reset();
            prefab.rootObject.sourcePrefabId.clear();
            const bool saved = prefabRegistry_.Save(
                prefabId,
                prefab,
                prefabSerializer_);
            SetStatus(
                saved
                    ? "Saved prefab: " + prefabId + "."
                    : "Failed to save prefab: " + prefabId + ".",
                !saved);
            return saved;
        }
        case SceneObjectCommandId::InstantiatePrefab: {
            PrefabDocument prefab{};
            const std::string prefabId(argument);
            if (!prefabRegistry_.Load(
                    prefabId,
                    prefab,
                    prefabSerializer_)) {
                SetStatus("Failed to load prefab: " + prefabId + ".", true);
                return false;
            }
            SceneObjectData instance = prefab.rootObject;
            const SceneObjectId instanceId{ context.nextSceneObjectId++ };
            instance.id = instanceId;
            instance.parent.reset();
            instance.sourcePrefabId = prefabId;
            if (instance.name.empty()) {
                instance.name = prefab.prefabName;
            }
            scene.GetSceneDocument().objects.push_back(std::move(instance));
            context.sceneDirty = true;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(
                scene,
                context.selection,
                context.nextSceneObjectId);
            context.selection.selectedObject =
                selectionSync.FindRuntimeObjectByDocumentId(
                    scene,
                    instanceId);
            historyRequest_ = MakeHistory(
                "Instantiate Prefab",
                std::move(beforeObjects),
                beforeCamera,
                dirtyBefore,
                scene);
            SetStatus("Instantiated prefab: " + prefabId + ".", false);
            return true;
        }
        case SceneObjectCommandId::AddComponent: {
            const ComponentTypeInfo* typeInfo =
                scene.GetComponentRegistry().Find(argument);
            const ComponentAddResult result =
                componentAuthoringService_.AddComponent(
                    scene.GetComponentRegistry(),
                    *target,
                    argument);
            SetStatus(result.message, !result.success);
            if (!result.success || !result.documentChanged) {
                return false;
            }
            context.sceneDirty = true;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(
                scene,
                context.selection,
                context.nextSceneObjectId);
            historyRequest_ = MakeHistory(
                "Add " +
                    (typeInfo != nullptr
                        ? typeInfo->presentation.displayName
                        : std::string(argument)),
                std::move(beforeObjects),
                beforeCamera,
                dirtyBefore,
                scene);
            return true;
        }
        }
        return false;
    }

    std::vector<std::string>
        SceneObjectCommandService::ListPrefabIds() const {
        std::vector<std::string> ids = prefabRegistry_.ListPrefabIds();
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    const std::string&
        SceneObjectCommandService::GetStatusMessage() const noexcept {
        return statusMessage_;
    }

    bool SceneObjectCommandService::IsStatusError() const noexcept {
        return statusIsError_;
    }

    void SceneObjectCommandService::ClearStatus() noexcept {
        statusMessage_.clear();
        statusIsError_ = false;
    }

    std::optional<SceneObjectAuthoringHistoryRequest>
        SceneObjectCommandService::ConsumeHistoryRequest() {
        std::optional<SceneObjectAuthoringHistoryRequest> request =
            std::move(historyRequest_);
        historyRequest_.reset();
        return request;
    }

    void SceneObjectCommandService::SetStatus(
        std::string message,
        bool error) {
        statusMessage_ = std::move(message);
        statusIsError_ = error;
    }

} // namespace HIKARI::EDITOR
