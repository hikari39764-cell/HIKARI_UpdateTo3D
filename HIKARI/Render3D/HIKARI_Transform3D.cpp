#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    MATH::Mat4 Transform3D::GetLocalMatrix() const {
        if (useExplicitMatrix) {
            return explicitMatrix;
        }
        return MATH::Mat4::TRS(position, MATH::NormalizeQ(rotation), scale);
    }

    MATH::Mat4 Transform3D::GetWorldMatrix() const {
        if (useExplicitMatrix) {
            return explicitMatrix;
        }
        if (!parent) {
            return GetLocalMatrix();
        }
        return parent->GetWorldMatrix() * GetLocalMatrix();
    }

} // namespace HIKARI
