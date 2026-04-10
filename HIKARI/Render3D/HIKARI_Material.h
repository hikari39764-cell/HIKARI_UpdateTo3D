#pragma once
#include <string>
#include "HIKARI_Math3D.h"

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

    private:
        MATH::Vec4 baseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string baseColorTexturePath_{};
        int baseColorTextureHandle_ = -1;
    };

} // namespace HIKARI
