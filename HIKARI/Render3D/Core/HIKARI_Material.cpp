#include "Render3D/HIKARI_Material.h"

#include <algorithm>
#include <utility>

namespace HIKARI {
    namespace {
        RuntimeTextureSlot kEmptyTextureSlot{};
    }

    void Material::SetBaseColor(const MATH::Vec4& color) {
        baseColor_ = color;
    }

    const MATH::Vec4& Material::GetBaseColor() const {
        return baseColor_;
    }

    void Material::SetBaseColorTexturePath(std::string path) {
        baseColorTexture_.resolvedPath = std::move(path);
        if (baseColorTexture_.sourcePath.empty()) {
            baseColorTexture_.sourcePath = baseColorTexture_.resolvedPath;
        }
        baseColorTexture_.enabled = !baseColorTexture_.resolvedPath.empty();
    }

    const std::string& Material::GetBaseColorTexturePath() const {
        return baseColorTexture_.resolvedPath;
    }

    void Material::SetBaseColorTextureHandle(int handle) {
        baseColorTexture_.resource = {};
        baseColorTexture_.handle = handle;
        baseColorTexture_.enabled = handle >= 0;
    }

    int Material::GetBaseColorTextureHandle() const {
        return baseColorTexture_.handle;
    }

    bool Material::HasBaseColorTexture() const {
        return baseColorTexture_.IsActive();
    }

    void Material::SetShaderProfileId(std::string shaderProfileId) {
        shaderProfileId_ = std::move(shaderProfileId);
    }

    const std::string& Material::GetShaderProfileId() const {
        return shaderProfileId_;
    }

    void Material::SetFeatureBits(uint32_t featureBits) {
        featureBits_ = featureBits;
    }

    uint32_t Material::GetFeatureBits() const {
        return featureBits_;
    }

    void Material::SetTextureSlot(ModelTextureUsage usage, RuntimeTextureSlot slot) {
        switch (usage) {
        case ModelTextureUsage::BaseColor:
            baseColorTexture_ = std::move(slot);
            break;
        case ModelTextureUsage::Normal:
            normalTexture_ = std::move(slot);
            break;
        case ModelTextureUsage::MetallicRoughness:
            metallicRoughnessTexture_ = std::move(slot);
            break;
        case ModelTextureUsage::Occlusion:
            occlusionTexture_ = std::move(slot);
            break;
        case ModelTextureUsage::Emissive:
            emissiveTexture_ = std::move(slot);
            break;
        default:
            break;
        }
    }

    const RuntimeTextureSlot& Material::GetTextureSlot(ModelTextureUsage usage) const {
        switch (usage) {
        case ModelTextureUsage::BaseColor:
            return baseColorTexture_;
        case ModelTextureUsage::Normal:
            return normalTexture_;
        case ModelTextureUsage::MetallicRoughness:
            return metallicRoughnessTexture_;
        case ModelTextureUsage::Occlusion:
            return occlusionTexture_;
        case ModelTextureUsage::Emissive:
            return emissiveTexture_;
        default:
            return kEmptyTextureSlot;
        }
    }

    bool Material::HasTextureSlot(ModelTextureUsage usage) const {
        return GetTextureSlot(usage).IsActive();
    }

    void Material::SetMetallicFactor(float value) {
        metallicFactor_ = std::clamp(value, 0.0f, 1.0f);
    }

    float Material::GetMetallicFactor() const {
        return metallicFactor_;
    }

    void Material::SetRoughnessFactor(float value) {
        roughnessFactor_ = std::clamp(value, 0.04f, 1.0f);
    }

    float Material::GetRoughnessFactor() const {
        return roughnessFactor_;
    }

    void Material::SetNormalScale(float value) {
        normalScale_ = value;
    }

    float Material::GetNormalScale() const {
        return normalScale_;
    }

    void Material::SetOcclusionStrength(float value) {
        occlusionStrength_ = value;
    }

    float Material::GetOcclusionStrength() const {
        return occlusionStrength_;
    }

    void Material::SetEmissiveFactor(const MATH::Vec3& value) {
        emissiveFactor_ = value;
    }

    const MATH::Vec3& Material::GetEmissiveFactor() const {
        return emissiveFactor_;
    }

    void Material::SetEmissiveStrength(float value) {
        emissiveStrength_ = value;
    }

    float Material::GetEmissiveStrength() const {
        return emissiveStrength_;
    }

} // namespace HIKARI
