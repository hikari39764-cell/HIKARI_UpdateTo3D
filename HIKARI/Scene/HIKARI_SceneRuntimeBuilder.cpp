#include "HIKARI_SceneRuntimeBuilder.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <numbers>
#include <unordered_set>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Lighting/HIKARI_LightingRuntimeLoader.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        struct RuntimeClusterGeometryLoadRequest {
            std::string sourceKey{};
            std::filesystem::path path{};
        };

        struct RuntimeAssetLoadPlan {
            std::vector<std::string> modelAssetIds{};
            std::vector<RENDER3D::TextureResourceLoadRequest> textureRequests{};
            std::vector<RuntimeClusterGeometryLoadRequest> clusterGeometryRequests{};
            std::unordered_set<std::string> textureKeys{};
            std::unordered_set<std::string> clusterGeometryKeys{};
        };

        std::string NormalizePlanPathKey(std::string value) {
            std::replace(value.begin(), value.end(), '\\', '/');
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        RENDER3D::TextureResourceColorSpace ColorSpaceForUsage(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor:
            case ModelTextureUsage::Emissive:
            case ModelTextureUsage::SpecularColor:
                return RENDER3D::TextureResourceColorSpace::Srgb;
            case ModelTextureUsage::Normal:
            case ModelTextureUsage::MetallicRoughness:
            case ModelTextureUsage::Occlusion:
            case ModelTextureUsage::Specular:
            default:
                return RENDER3D::TextureResourceColorSpace::Linear;
            }
        }

        const char* UsageName(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor: return "baseColor";
            case ModelTextureUsage::Normal: return "normal";
            case ModelTextureUsage::MetallicRoughness: return "metallicRoughness";
            case ModelTextureUsage::Occlusion: return "occlusion";
            case ModelTextureUsage::Emissive: return "emissive";
            case ModelTextureUsage::Specular: return "specular";
            case ModelTextureUsage::SpecularColor: return "specularColor";
            default: return "unknown";
            }
        }

        const TextureAsset3D* FindTextureBySlot(const ModelAsset& asset, const TextureSlot& slot) {
            if (slot.textureIndex < 0 || slot.textureIndex >= static_cast<int>(asset.textures.size())) {
                return nullptr;
            }
            return &asset.textures[static_cast<size_t>(slot.textureIndex)];
        }

        const std::string& SelectRuntimeTexturePath(const TextureAsset3D& texture) {
            return texture.resolvedPath.empty() ? texture.sourcePath : texture.resolvedPath;
        }

        void AddTextureRequest(
            RuntimeAssetLoadPlan& plan,
            std::string name,
            std::string path,
            RENDER3D::TextureResourceColorSpace colorSpace) {

            if (path.empty()) {
                return;
            }

            const std::string key =
                NormalizePlanPathKey(path) + "|" +
                std::to_string(static_cast<int>(colorSpace));
            if (!plan.textureKeys.insert(key).second) {
                return;
            }

            plan.textureRequests.push_back(RENDER3D::TextureResourceLoadRequest{
                std::move(name),
                std::move(path),
                colorSpace });
        }

        void AddModelTextureSlot(
            RuntimeAssetLoadPlan& plan,
            const ModelAsset& asset,
            const MaterialAsset& material,
            const TextureSlot& slot,
            ModelTextureUsage usage) {

            const TextureAsset3D* texture = FindTextureBySlot(asset, slot);
            if (texture == nullptr) {
                return;
            }

            const std::string& path = SelectRuntimeTexturePath(*texture);
            AddTextureRequest(
                plan,
                "runtime_model/" + asset.GetName() + "/" + material.name + "/" + UsageName(usage),
                path,
                ColorSpaceForUsage(usage));
        }

        void CollectModelTextureRequests(
            RuntimeAssetLoadPlan& plan,
            const ModelAsset& asset) {

            for (const MaterialAsset& material : asset.materials) {
                AddModelTextureSlot(plan, asset, material, material.baseColorTexture, ModelTextureUsage::BaseColor);
                AddModelTextureSlot(plan, asset, material, material.normalTexture, ModelTextureUsage::Normal);
                AddModelTextureSlot(plan, asset, material, material.metallicRoughnessTexture, ModelTextureUsage::MetallicRoughness);
                AddModelTextureSlot(plan, asset, material, material.occlusionTexture, ModelTextureUsage::Occlusion);
                AddModelTextureSlot(plan, asset, material, material.emissiveTexture, ModelTextureUsage::Emissive);
                AddModelTextureSlot(plan, asset, material, material.specularTexture, ModelTextureUsage::Specular);
                AddModelTextureSlot(plan, asset, material, material.specularColorTexture, ModelTextureUsage::SpecularColor);
            }
        }

        void AddMaterialTextureSlot(
            RuntimeAssetLoadPlan& plan,
            const AssetRegistry& assetRegistry,
            const std::string& materialId,
            const MaterialTextureSlotData& slot,
            ModelTextureUsage usage) {

            if (!slot.useTexture || !slot.textureAssetGuid.IsValid()) {
                return;
            }

            const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(
                AssetId{ slot.textureAssetGuid.value });
            if (texture == nullptr || texture->sourcePath.empty()) {
                return;
            }

            AddTextureRequest(
                plan,
                "runtime_material/" + materialId + "/" + UsageName(usage),
                texture->sourcePath,
                ColorSpaceForUsage(usage));
        }

        void CollectMaterialTextureRequests(
            RuntimeAssetLoadPlan& plan,
            const AssetRegistry& assetRegistry,
            const MaterialAssetDescriptor& descriptor) {

            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.baseColorTexture, ModelTextureUsage::BaseColor);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.normalTexture, ModelTextureUsage::Normal);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.metallicRoughnessTexture, ModelTextureUsage::MetallicRoughness);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.occlusionTexture, ModelTextureUsage::Occlusion);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.emissiveTexture, ModelTextureUsage::Emissive);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.specularTexture, ModelTextureUsage::Specular);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.specularColorTexture, ModelTextureUsage::SpecularColor);
        }

        std::filesystem::path ResolveProjectPath(
            const std::filesystem::path& projectRoot,
            const std::string& path) {

            std::filesystem::path resolved = path;
            if (!resolved.is_absolute() && !projectRoot.empty()) {
                resolved = projectRoot / resolved;
            }
            return resolved.lexically_normal();
        }

        void AddClusterGeometryRequest(
            RuntimeAssetLoadPlan& plan,
            const std::filesystem::path& projectRoot,
            const ModelAssetDescriptor& descriptor) {

            if (descriptor.clusteredGeometryPath.empty()) {
                return;
            }

            const std::filesystem::path resolvedPath =
                ResolveProjectPath(projectRoot, descriptor.clusteredGeometryPath);
            const std::string renderPath = resolvedPath.generic_string();
            const std::string sourceKey =
                RENDER3D::GPUDRIVEN::BuildGpuSceneClusterGeometrySourceKey(renderPath);
            if (sourceKey.empty()) {
                return;
            }

            const std::string dedupeKey = sourceKey + "|" + NormalizePlanPathKey(renderPath);
            if (!plan.clusterGeometryKeys.insert(dedupeKey).second) {
                return;
            }

            plan.clusterGeometryRequests.push_back(RuntimeClusterGeometryLoadRequest{
                sourceKey,
                resolvedPath });
        }

        void ExecuteRuntimeAssetLoadPlan(RuntimeAssetLoadPlan& plan) {
            if (!plan.textureRequests.empty()) {
                RENDER3D::TextureResourceBatchLoadStats textureStats{};
                RENDER3D::PreloadTextureResourcesWithColorSpace(
                    plan.textureRequests,
                    &textureStats);

                HIKARI_LOG_INFO(
                    "[SceneRuntimeBuilder][LoadPlan][Texture] requested=" + std::to_string(textureStats.requested) +
                    " cacheHits=" + std::to_string(textureStats.cacheHits) +
                    " uploaded=" + std::to_string(textureStats.uploaded) +
                    " registered=" + std::to_string(textureStats.registered) +
                    " failed=" + std::to_string(textureStats.failed) +
                    " uploadBytes=" + std::to_string(textureStats.uploadedBytes));
            }

            uint32_t clusterLoaded = 0;
            uint32_t clusterFailed = 0;
            for (const RuntimeClusterGeometryLoadRequest& request : plan.clusterGeometryRequests) {
                const RENDER3D::ClusterGeometryResourceHandle handle =
                    RENDER3D::LoadClusterGeometryResource(request.sourceKey, request.path);
                if (handle) {
                    ++clusterLoaded;
                } else {
                    ++clusterFailed;
                }
            }
            if (!plan.clusterGeometryRequests.empty()) {
                HIKARI_LOG_INFO(
                    "[SceneRuntimeBuilder][LoadPlan][ClusterGeometry] requested=" +
                    std::to_string(plan.clusterGeometryRequests.size()) +
                    " loaded=" + std::to_string(clusterLoaded) +
                    " failed=" + std::to_string(clusterFailed));
            }
        }

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

        ModelAsset* ResolveModelAssetNow(
            ModelManager& modelManager,
            const AssetRegistry& assetRegistry,
            const std::string& assetId) {

            if (assetId.empty()) {
                return nullptr;
            }

            const auto* descriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ assetId });
            if (descriptor != nullptr) {
                EnsureModelLoaded(modelManager, *descriptor);
                return modelManager.FindAsset(assetId);
            }

            ModelAsset* existing = modelManager.FindAsset(assetId);
            if (existing != nullptr && existing->GetState() == ModelAsset::State::Unloaded) {
                modelManager.LoadAssetNow(assetId);
            }
            return modelManager.FindAsset(assetId);
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

        REFLECTION::RuntimeReflectionProbeInfluenceShape ToRuntimeInfluenceShape(
            ReflectionProbeInfluenceShape shape) {

            return shape == ReflectionProbeInfluenceShape::Box
                ? REFLECTION::RuntimeReflectionProbeInfluenceShape::Box
                : REFLECTION::RuntimeReflectionProbeInfluenceShape::Sphere;
        }

        REFLECTION::RuntimeReflectionProbeProjectionShape ToRuntimeProjectionShape(
            ReflectionProbeProjectionShape shape) {

            return shape == ReflectionProbeProjectionShape::Box
                ? REFLECTION::RuntimeReflectionProbeProjectionShape::Box
                : REFLECTION::RuntimeReflectionProbeProjectionShape::Infinite;
        }
    }

    SceneDependencySet SceneRuntimeBuilder::CollectDependencies(const SceneDocument& document) const {
        SceneDependencySet deps{};

        if (!document.environment.sky.skyAsset.empty()) {
            deps.skyAssetIds.insert(document.environment.sky.skyAsset);
        }
        if (document.environment.reflectionProbe.enabled &&
            !document.environment.reflectionProbe.sourceCubemapAsset.empty()) {
            deps.reflectionProbeCubemapAssetIds.insert(document.environment.reflectionProbe.sourceCubemapAsset);
            deps.reflectionProbeEnabled = true;
            deps.reflectionProbePosition = document.environment.reflectionProbe.position;
            deps.reflectionProbeRadius = document.environment.reflectionProbe.radius;
            deps.reflectionProbeIntensity = document.environment.reflectionProbe.intensity;
            deps.reflectionProbeInfluenceShape = document.environment.reflectionProbe.influenceShape;
            deps.reflectionProbeProjectionShape = document.environment.reflectionProbe.projectionShape;
            deps.reflectionProbeInfluenceBoxCenter = document.environment.reflectionProbe.influenceBoxCenter;
            deps.reflectionProbeInfluenceBoxSize = document.environment.reflectionProbe.influenceBoxSize;
            deps.reflectionProbeProjectionBoxCenter = document.environment.reflectionProbe.projectionBoxCenter;
            deps.reflectionProbeProjectionBoxSize = document.environment.reflectionProbe.projectionBoxSize;
            deps.reflectionProbeBlendDistance = document.environment.reflectionProbe.blendDistance;
            deps.reflectionProbePriority = document.environment.reflectionProbe.priority;
        }
        LightProbeVolumeSettings lightProbe = document.lightingBake.lightProbeVolume;
        ClampLightProbeVolumeSettings(lightProbe);
        deps.lightProbeVolumeEnabled = lightProbe.enabled;
        deps.lightProbeVolumeIntensity = lightProbe.intensity;

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
        SkyManager& skyManager,
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid) const {
        RuntimeAssetLoadPlan loadPlan{};

        auto addModelToLoadPlan = [&](
            const std::string& modelId,
            const ModelAssetDescriptor& descriptor) {

            loadPlan.modelAssetIds.push_back(modelId);
            EnsureModelLoaded(modelManager, descriptor);
            if (const ModelAsset* asset = modelManager.FindAsset(modelId);
                asset != nullptr && asset->GetState() == ModelAsset::State::Loaded) {
                CollectModelTextureRequests(loadPlan, *asset);
            }
            AddClusterGeometryRequest(loadPlan, projectRoot, descriptor);
        };

        for (const std::string& modelId : dependencies.modelAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ modelId });
            if (!descriptor) {
                continue;
            }
            addModelToLoadPlan(modelId, *descriptor);
        }

        for (const std::string& skyId : dependencies.skyAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<SkyAssetDescriptor>(AssetId{ skyId });
            if (!descriptor) {
                continue;
            }

            if (!descriptor->meshAssetId.empty()) {
                if (const auto* meshDescriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ descriptor->meshAssetId })) {
                    addModelToLoadPlan(descriptor->meshAssetId, *meshDescriptor);
                }
            }
        }

        for (const std::string& materialId : dependencies.materialAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<MaterialAssetDescriptor>(AssetId{ materialId });
            if (descriptor == nullptr) {
                continue;
            }
            CollectMaterialTextureRequests(loadPlan, assetRegistry, *descriptor);
        }

        HIKARI_LOG_INFO(
            "[SceneRuntimeBuilder][LoadPlan] models=" + std::to_string(loadPlan.modelAssetIds.size()) +
            " textures=" + std::to_string(loadPlan.textureRequests.size()) +
            " clusterGeometry=" + std::to_string(loadPlan.clusterGeometryRequests.size()));
        ExecuteRuntimeAssetLoadPlan(loadPlan);

        RENDER3D::LIGHTING::LightingRuntimeLoadRequest request{};
        request.sceneGuid = sceneGuid;
        request.projectRoot = projectRoot;
        request.skyAssetIds = dependencies.skyAssetIds;
        request.reflectionProbeCubemapAssetIds = dependencies.reflectionProbeCubemapAssetIds;
        request.reflectionProbeEnabled = dependencies.reflectionProbeEnabled;
        request.reflectionProbePosition = dependencies.reflectionProbePosition;
        request.reflectionProbeRadius = dependencies.reflectionProbeRadius;
        request.reflectionProbeIntensity = dependencies.reflectionProbeIntensity;
        request.reflectionProbeInfluenceShape = ToRuntimeInfluenceShape(dependencies.reflectionProbeInfluenceShape);
        request.reflectionProbeProjectionShape = ToRuntimeProjectionShape(dependencies.reflectionProbeProjectionShape);
        request.reflectionProbeInfluenceBoxCenter = dependencies.reflectionProbeInfluenceBoxCenter;
        request.reflectionProbeInfluenceBoxSize = dependencies.reflectionProbeInfluenceBoxSize;
        request.reflectionProbeProjectionBoxCenter = dependencies.reflectionProbeProjectionBoxCenter;
        request.reflectionProbeProjectionBoxSize = dependencies.reflectionProbeProjectionBoxSize;
        request.reflectionProbeBlendDistance = dependencies.reflectionProbeBlendDistance;
        request.reflectionProbePriority = dependencies.reflectionProbePriority;
        request.lightProbeVolumeEnabled = dependencies.lightProbeVolumeEnabled;
        request.lightProbeVolumeIntensity = dependencies.lightProbeVolumeIntensity;

        // 照明リソースは runtime loader に委譲する。
        RENDER3D::LIGHTING::LightingRuntimeLoader loader{};
        const RENDER3D::LIGHTING::LightingRuntimeLoadResult lightingResult =
            loader.Load(request, assetRegistry, skyManager);
        for (const std::string& message : lightingResult.messages) {
            HIKARI_LOG_INFO(message);
        }
        return lightingResult.success;
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
                    HIKARI_LOG_WARN(
                        "[SceneRuntime] unavailable component skipped: " +
                        componentData.type +
                        " objectId=" + std::to_string(objectData.id.value));
                    continue;
                }
                component->Deserialize(componentData.properties);

                if (auto* modelComponent = dynamic_cast<ModelComponent*>(component)) {
                    modelComponent->SetModelAsset(ResolveModelAssetNow(
                        modelManager,
                        assetRegistry,
                        modelComponent->GetAssetId()));
                    BuildModelMaterialOverride(*modelComponent, assetRegistry);
                }
            }
        }

        return true;
    }

} // namespace HIKARI
