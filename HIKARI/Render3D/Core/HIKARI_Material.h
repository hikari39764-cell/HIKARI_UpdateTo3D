#pragma once
#include <string>
#include <cstdint>
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class Material {
    public:
        void SetBaseColor(const MATH::Vec4& color);
        const MATH::Vec4& GetBaseColor() const;
        void SetBaseColorTexturePath(std::string path);
        const std::string& GetBaseColorTexturePath() const;
        void SetBaseColorTextureHandle(int handle);
        int GetBaseColorTextureHandle() const;
        bool HasBaseColorTexture() const;

        void SetShaderProfileId(std::string shaderProfileId);
        const std::string& GetShaderProfileId() const;
        void SetFeatureBits(uint32_t featureBits);
        uint32_t GetFeatureBits() const;

    private:
        MATH::Vec4 baseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string baseColorTexturePath_{};
        int baseColorTextureHandle_ = -1;
        std::string shaderProfileId_{};
        uint32_t featureBits_ = 0;
    };

} // namespace HIKARI
