#include "HIKARI_SceneRuntimeBuilder.h"

#include <memory>
#include <numbers>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        void EnsureModelLoaded(ModelManager& modelManager, const ModelAssetDescriptor& descriptor) {
            ModelAsset* existing = modelManager.FindAsset(descriptor.id.value);
            if (existing != nullptr) {
                if (existing->GetSourcePath() == descriptor.sourcePath && existing->GetState() == ModelAsset::State::Loaded) {
                    return;
                }

                if (existing->GetSourcePath() == descriptor.sourcePath && existing->GetState() == ModelAsset::State::Unloaded) {
                    modelManager.LoadAssetNow(descriptor.id.value);
                    return;
                }
            }

            modelManager.RegisterAsset(descriptor.id.value, descriptor.sourcePath);
            modelManager.LoadAssetNow(descriptor.id.value);
        }

        void BuildModelMaterialOverride(
            ModelComponent& modelComponent,
            const AssetRegistry& assetRegistry) {

            modelComponent.ClearRuntimeMaterialOverride();
            for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                if (slot.slotIndex != 0 || !slot.materialAssetGuid.IsValid()) {
                    continue;
                }

                const auto* descriptor = assetRegistry.FindAs<MaterialAssetDescriptor>(
                    AssetId{ slot.materialAssetGuid.value });
                if (!descriptor) {
                    continue;
                }

                auto runtimeMaterial = std::make_unique<Material>();
                MaterialRuntimeBuilder builder{};
                // 現段階では slot 0 をモデル全体に適用する。
                if (builder.BuildRuntimeMaterial(
                        descriptor->data,
                        assetRegistry,
                        *runtimeMaterial,
                        descriptor->id.value)) {
                    modelComponent.SetRuntimeMaterialOverride(
                        std::move(runtimeMaterial),
                        slot.materialAssetGuid);
                }
                return;
            }
        }
    }

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
                    if (component.properties.contains("materialOverrides") &&
                        component.properties["materialOverrides"].is_array()) {
                        for (const nlohmann::json& node : component.properties["materialOverrides"]) {
                            if (!node.is_object()) {
                                continue;
                            }
                            const std::string materialGuid = node.value("materialAssetGuid", std::string{});
                            if (!materialGuid.empty()) {
                                deps.materialAssetIds.insert(materialGuid);
                            }
                        }
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
            EnsureModelLoaded(modelManager, *descriptor);
        }

        for (const std::string& skyId : dependencies.skyAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<SkyAssetDescriptor>(AssetId{ skyId });
            if (!descriptor) {
                continue;
            }

            if (!descriptor->meshAssetId.empty()) {
                if (const auto* meshDescriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ descriptor->meshAssetId })) {
                    EnsureModelLoaded(modelManager, *meshDescriptor);
                }
            }

            std::string texturePath = descriptor->sourcePath;
            if (!descriptor->textureAssetId.empty()) {
                if (const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(AssetId{ descriptor->textureAssetId })) {
                    texturePath = texture->sourcePath;
                }
            }

            skyManager.RegisterOrUpdateAsset(SkyAsset{
                descriptor->id.value,
                descriptor->meshAssetId,
                texturePath,
                descriptor->preferredMode
            });
        }

        return true;
    }

    bool SceneRuntimeBuilder::BuildWorldFromDocument(
        const SceneDocument& document,
        World& world,
        const AssetRegistry& assetRegistry,
        const ComponentRegistry& componentRegistry,
        ModelManager& modelManager,
        SkyManager&) const {
        world.Clear();

        constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

        for (const SceneObjectData& objectData : document.objects) {
            GameObject* object = world.CreateObject(objectData.name);
            object->SetDocumentId(objectData.id);
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
                    modelComponent->SetModelAsset(modelManager.FindAsset(modelComponent->GetAssetId()));
                    BuildModelMaterialOverride(*modelComponent, assetRegistry);
                }
            }
        }

        return true;
    }

} // namespace HIKARI
