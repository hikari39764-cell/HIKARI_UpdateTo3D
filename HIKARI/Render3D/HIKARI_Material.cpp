#include "HIKARI_Material.h"

namespace HIKARI {

    void Material::SetBaseColor(const MATH::Vec4& color) {
        baseColor_ = color;
    }

    const MATH::Vec4& Material::GetBaseColor() const {
        return baseColor_;
    }

} // namespace HIKARI
