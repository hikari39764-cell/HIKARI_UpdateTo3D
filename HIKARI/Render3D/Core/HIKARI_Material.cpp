#include "Render3D/HIKARI_Material.h"
#include <utility>

namespace HIKARI {

    void Material::SetBaseColor(const MATH::Vec4& color) {
        baseColor_ = color;
    }

    const MATH::Vec4& Material::GetBaseColor() const {
        return baseColor_;
    }

    void Material::SetBaseColorTexturePath(std::string path) {
        baseColorTexturePath_ = std::move(path);
    }

    const std::string& Material::GetBaseColorTexturePath() const {
        return baseColorTexturePath_;
    }

    void Material::SetBaseColorTextureHandle(int handle) {
        baseColorTextureHandle_ = handle;
    }

    int Material::GetBaseColorTextureHandle() const {
        return baseColorTextureHandle_;
    }

    bool Material::HasBaseColorTexture() const {
        return baseColorTextureHandle_ >= 0;
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

} // namespace HIKARI
