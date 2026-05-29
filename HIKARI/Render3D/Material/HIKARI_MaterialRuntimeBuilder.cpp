#include "HIKARI_MaterialRuntimeBuilder.h"

#include <string>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "HIKARI_DxTexture.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"

namespace HIKARI {

    namespace {
        DXTEX::TextureColorSpace ColorSpaceForUsage(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor:
            case ModelTextureUsage::Emissive:
                return DXTEX::TextureColorSpace::Srgb;
            case ModelTextureUsage::Normal:
            case ModelTextureUsage::MetallicRoughness:
            case ModelTextureUsage::Occlusion:
            default:
                return DXTEX::TextureColorSpace::Linear;
            }
        }

        const char* UsageName(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor: return "baseColor";
            case ModelTextureUsage::Normal: return "normal";
            case ModelTextureUsage::MetallicRoughness: return "metallicRoughness";
            case ModelTextureUsage::Occlusion: return "occlusion";
            case ModelTextureUsage::Emissive: return "emissive";
            default: return "unknown";
            }
        }

        RuntimeTextureSlot DefaultSlotForUsage(ModelTextureUsage usage)
        {
            switch (usage) {
            case ModelTextureUsage::BaseColor:
            case ModelTextureUsage::Occlusion:
                return DefaultPbrResources::WhiteSlot();
            case ModelTextureUsage::Normal:
                return DefaultPbrResources::FlatNormalSlot();
            case ModelTextureUsage::MetallicRoughness:
                return DefaultPbrResources::MetallicRoughnessSlot();
            case ModelTextureUsage::Emissive:
                return DefaultPbrResources::BlackSlot();
            default:
                return DefaultPbrResources::MissingSlot();
            }
        }

        RuntimeTextureSlot ActiveMissingSlot()
        {
            RuntimeTextureSlot slot = DefaultPbrResources::MissingSlot();
            slot.enabled = true;
            return slot;
        }

        RuntimeTextureSlot BuildSlot(
            const MaterialTextureSlotData& slotData,
            ModelTextureUsage usage,
            const AssetRegistry& assetRegistry,
            std::string_view debugName) {

            if (!slotData.useTexture || !slotData.textureAssetGuid.IsValid()) {
                return DefaultSlotForUsage(usage);
            }

            const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(
                AssetId{ slotData.textureAssetGuid.value });
            if (!texture || texture->sourcePath.empty()) {
                HIKARI_LOG_WARN("[MaterialRuntimeBuilder] texture asset not resolved: " +
                    slotData.textureAssetGuid.value);
                return ActiveMissingSlot();
            }

            RuntimeTextureSlot slot{};
            slot.sourcePath = texture->sourcePath;
            slot.resolvedPath = texture->sourcePath;
            const std::string textureName =
                "material_asset/" + std::string(debugName) + "/" + UsageName(usage);
            slot.handle = DXTEX::DxTextureManager::LoadTextureWithColorSpace(
                textureName,
                slot.resolvedPath,
                ColorSpaceForUsage(usage));
            if (slot.handle < 0) {
                HIKARI_LOG_WARN("[MaterialRuntimeBuilder] texture load failed: " +
                    slot.resolvedPath);
                return ActiveMissingSlot();
            }

            slot.enabled = true;
            return slot;
        }
    }

    bool MaterialRuntimeBuilder::BuildRuntimeMaterial(
        const PbrMaterialAssetData& data,
        const AssetRegistry& assetRegistry,
        Material& outMaterial,
        std::string_view debugName) const {

        outMaterial.SetBaseColor(data.baseColorFactor);
        outMaterial.SetMetallicFactor(data.metallicFactor);
        outMaterial.SetRoughnessFactor(data.roughnessFactor);
        outMaterial.SetNormalScale(data.normalScale);
        outMaterial.SetOcclusionStrength(data.occlusionStrength);
        outMaterial.SetEmissiveFactor(data.emissiveFactor);
        outMaterial.SetEmissiveStrength(data.emissiveStrength);
        outMaterial.SetShaderProfileId("PBR");

        uint32_t featureBits = 0;
        if (data.unlit) {
            featureBits |= MATERIAL_FEATURES::Unlit;
        }
        if (data.emissiveTexture.useTexture || data.emissiveStrength > 0.0f) {
            featureBits |= MATERIAL_FEATURES::Emissive;
        }
        outMaterial.SetFeatureBits(featureBits);

        // Material JSON は GUID だけを保持し、Runtime では Registry から cooked texture へ解決する。
        outMaterial.SetTextureSlot(ModelTextureUsage::BaseColor,
            BuildSlot(data.baseColorTexture, ModelTextureUsage::BaseColor, assetRegistry, debugName));
        outMaterial.SetTextureSlot(ModelTextureUsage::Normal,
            BuildSlot(data.normalTexture, ModelTextureUsage::Normal, assetRegistry, debugName));
        outMaterial.SetTextureSlot(ModelTextureUsage::MetallicRoughness,
            BuildSlot(data.metallicRoughnessTexture, ModelTextureUsage::MetallicRoughness, assetRegistry, debugName));
        outMaterial.SetTextureSlot(ModelTextureUsage::Occlusion,
            BuildSlot(data.occlusionTexture, ModelTextureUsage::Occlusion, assetRegistry, debugName));
        outMaterial.SetTextureSlot(ModelTextureUsage::Emissive,
            BuildSlot(data.emissiveTexture, ModelTextureUsage::Emissive, assetRegistry, debugName));

        return true;
    }

} // namespace HIKARI
