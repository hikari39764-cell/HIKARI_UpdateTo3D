#include "Render3D/Core/HIKARI_MeshVariantResolver.h"

#include <utility>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI::MESHRENDERER {

    namespace {
        void ApplyMaterialFxOverride(DrawItem& item) {
            item.hasResolvedMaterialFxProfile = false;
            item.resolvedMaterialFxProfile = {};

            if (item.materialFxProfileId.empty()) {
                return;
            }

            MaterialFxProfile profile{};
            if (!MaterialFxProfile::LoadById(item.materialFxProfileId, profile)) {
                return;
            }

            item.hasResolvedMaterialFxProfile = true;
            item.resolvedMaterialFxProfile = std::move(profile);
            ApplyProfileToVariant(item.resolvedMaterialFxProfile, item.variant);
        }
    }

    void ApplyProfileToVariant(const MaterialFxProfile& profile, VFX::VariantKey& variant) {
        if (!profile.shaderProfileId.empty()) {
            variant.shaderId = profile.shaderProfileId;
        }
        if (!profile.vertexShaderId.empty()) {
            variant.vertexShaderId = profile.vertexShaderId;
        }
        if (!profile.pixelShaderId.empty()) {
            variant.pixelShaderId = profile.pixelShaderId;
        }
        variant.featureBits = profile.featureBits;
        variant.composite = profile.composite;
        variant.depthTest = profile.depthTest;
        variant.depthWrite = profile.depthWrite;
        variant.doubleSided = profile.doubleSided;
    }

    void ResolveDrawVariant(DrawItem& item) {
        if (!item.asset) {
            return;
        }
        const Material* material = item.asset->GetMaterial();
        if (material) {
            item.variant.shaderId = material->GetShaderProfileId();
            item.variant.featureBits = material->GetFeatureBits();
        }
        item.variant.composite = VFX::CompositeMode::Alpha;
        item.variant.depthTest = true;
        item.variant.depthWrite = true;
        item.variant.doubleSided = false;

        ApplyMaterialFxOverride(item);

        item.fxValues = {};
        item.fxFlags = item.variant.featureBits;

        // 先使用 profile 默认值
        if (item.hasResolvedMaterialFxProfile) {
            const auto& defaults = item.resolvedMaterialFxProfile.values;
            for (size_t i = 0; i < item.fxValues.size() && i < defaults.size(); ++i) {
                const DirectX::XMFLOAT4& value = defaults[i];
                item.fxValues[i] = { value.x, value.y, value.z, value.w };
            }
        }

        // 如果外部 / Editor 有自定义参数，再覆盖默认值
        if (item.materialFxValuesInitialized) {
            for (size_t i = 0; i < item.fxValues.size(); ++i) {
                const DirectX::XMFLOAT4& value = item.materialFxParamValues[i];
                item.fxValues[i] = { value.x, value.y, value.z, value.w };
            }
        }
        item.fxFlags = item.variant.featureBits;
    }

    VFX::VariantKey ResolvePrimitiveVariant(
        const DrawItem& item,
        const MaterialAsset* materialAsset) {
        VFX::VariantKey variant = item.variant;

        if (materialAsset != nullptr) {
            variant.shaderId = materialAsset->shaderProfileId;
            variant.featureBits = materialAsset->featureBits;
            variant.doubleSided = materialAsset->doubleSided;
        }

        if (item.hasResolvedMaterialFxProfile) {
            ApplyProfileToVariant(item.resolvedMaterialFxProfile, variant);
        }

        return variant;
    }

} // namespace HIKARI::MESHRENDERER
