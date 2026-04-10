#include "HIKARI_Transform3D.h"

namespace HIKARI {

    MATH::Mat4 Transform3D::GetLocalMatrix() const {
        return MATH::Mat4::TRS(position, MATH::Normalize(rotation), scale);
    }

    MATH::Mat4 Transform3D::GetWorldMatrix() const {
        if (!parent) {
            return GetLocalMatrix();
        }
        return parent->GetWorldMatrix() * GetLocalMatrix();
    }

} // namespace HIKARI
