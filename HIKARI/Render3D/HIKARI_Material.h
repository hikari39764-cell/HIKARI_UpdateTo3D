#pragma once
#include "HIKARI_Math3D.h"

namespace HIKARI {

    class Material {
    public:
        void SetBaseColor(const MATH::Vec4& color);
        const MATH::Vec4& GetBaseColor() const;

    private:
        MATH::Vec4 baseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

} // namespace HIKARI
