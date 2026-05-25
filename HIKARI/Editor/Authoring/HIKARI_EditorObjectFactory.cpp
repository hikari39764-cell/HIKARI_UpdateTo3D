#include "HIKARI_EditorObjectFactory.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

    namespace {
        SceneObjectId AllocateSceneObjectId(const SceneDocument& document) {
            uint64_t maxId = 0;
            for (const SceneObjectData& object : document.objects) {
                maxId = (std::max)(maxId, object.id.value);
            }
            return SceneObjectId{ maxId + 1 };
        }

        GameObject* FindRuntimeObject(DocumentSceneBase& scene, SceneObjectId id) {
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (object && object->GetDocumentId() == id) {
                    return object.get();
                }
            }
            return nullptr;
        }

        std::string ResolveObjectName(
            const DocumentSceneBase& scene,
            const AssetGuid* modelGuid,
            const CreateObjectRequest& request,
            SceneObjectId id) {

            if (!request.name.empty()) {
                return request.name;
            }
            if (modelGuid && modelGuid->IsValid()) {
                if (const AssetRecord* record = scene.GetAssetDatabase().FindByGuid(*modelGuid)) {
                    if (!record->displayName.empty()) {
                        return record->displayName;
                    }
                    if (!record->sourcePath.empty()) {
                        return record->sourcePath.stem().string();
                    }
                }
            }
            return "GameObject_" + std::to_string(id.value);
        }

        SceneObjectData BuildBaseObject(
            DocumentSceneBase& scene,
            const AssetGuid* modelGuid,
            const CreateObjectRequest& request) {

            SceneObjectData object{};
            object.id = AllocateSceneObjectId(scene.GetSceneDocument());
            object.name = ResolveObjectName(scene, modelGuid, request, object.id);
            object.transform.position = request.position;
            object.transform.rotationEulerDeg = request.rotation;
            object.transform.scale = request.scale;
            return object;
        }
    }

    GameObject* CreateEmptyObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request) {

        SceneObjectData object = BuildBaseObject(scene, nullptr, request);
        const SceneObjectId id = object.id;
        scene.GetSceneDocument().objects.push_back(std::move(object));

        // Document を正として runtime world を作り直す。
        scene.RebuildRuntimeWorld();
        return FindRuntimeObject(scene, id);
    }

    GameObject* CreateModelObject(
        DocumentSceneBase& scene,
        const AssetGuid& modelGuid,
        const CreateObjectRequest& request) {

        SceneObjectData object = BuildBaseObject(scene, &modelGuid, request);
        const SceneObjectId id = object.id;

        SceneComponentData modelComponent{};
        modelComponent.type = "ModelComponent";
        modelComponent.properties = {
            { "assetId", modelGuid.value },
            { "visible", true },
            { "sourceKind", "Asset" },
            { "castShadow", true },
            { "receiveShadow", true }
        };
        object.components.push_back(std::move(modelComponent));

        scene.GetSceneDocument().objects.push_back(std::move(object));

        // Document を正として runtime world を作り直す。
        scene.RebuildRuntimeWorld();
        return FindRuntimeObject(scene, id);
    }

} // namespace HIKARI::EDITOR
