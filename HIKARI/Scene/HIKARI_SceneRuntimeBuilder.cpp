#include "HIKARI_SceneRuntimeBuilder.h"

#include <numbers>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/HIKARI_ModelManager.h"
#include "Render3D/HIKARI_SkyManager.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    SceneDependencySet SceneRuntimeBuilder::CollectDependencies(const SceneDocument& document) const {
        SceneDependencySet deps{};

        if (!document.environment.sky.skyAsset.empty()) {
            deps.skyAssetIds.insert(document.environment.sky.skyAsset);
        }

        for (const SceneObjectData& object : document.objects) {
            for (const SceneComponentData& component : object.components) {
                if (component.type == "ModelComponent") {
                    const std::string assetId = component.properties.value("assetId", std::string{});
                    if (!assetId.empty()) {
                        deps.modelAssetIds.insert(assetId);
                    }
                }
            }
        }

        return deps;
    }

    bool SceneRuntimeBuilder::PreloadDependencies(
        const SceneDependencySet& dependencies,
        const AssetRegistry& assetRegistry,
        ModelManager& modelManager,
        SkyManager& skyManager) const {
        for (const std::string& modelId : dependencies.modelAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ modelId });
            if (!descriptor) {
                continue;
            }
            modelManager.RegisterAsset(descriptor->id.value, descriptor->sourcePath);
            modelManager.LoadAssetNow(descriptor->id.value);
        }

        for (const std::string& skyId : dependencies.skyAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<SkyAssetDescriptor>(AssetId{ skyId });
            if (!descriptor) {
                continue;
            }

            const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(AssetId{ descriptor->textureAssetId });
            const std::string texturePath = texture ? texture->sourcePath : std::string{};
            skyManager.RegisterAsset(SkyAsset{ descriptor->id.value, descriptor->meshAssetId, texturePath });
        }

        return true;
    }

    bool SceneRuntimeBuilder::BuildWorldFromDocument(
        const SceneDocument& document,
        World& world,
        const AssetRegistry&,
        const ComponentRegistry& componentRegistry,
        ModelManager& modelManager,
        SkyManager&) const {
        world.Clear();

        constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

        for (const SceneObjectData& objectData : document.objects) {
            GameObject* object = world.CreateObject(objectData.name);
            object->Transform().position = objectData.transform.position;
            object->Transform().scale = objectData.transform.scale;
            object->Transform().rotation = MATH::Quat::FromEulerXYZ(
                objectData.transform.rotationEulerDeg.x * kDegToRad,
                objectData.transform.rotationEulerDeg.y * kDegToRad,
                objectData.transform.rotationEulerDeg.z * kDegToRad);

            for (const SceneComponentData& componentData : objectData.components) {
                IComponent* component = componentRegistry.AddComponentToObject(*object, componentData.type);
                if (!component) {
                    continue;
                }
                component->Deserialize(componentData.properties);

                if (auto* modelComponent = dynamic_cast<ModelComponent*>(component)) {
                    modelComponent->SetAsset(modelManager.FindAsset(modelComponent->GetAssetId()));
                }
            }
        }

        return true;
    }

} // namespace HIKARI
