#pragma once
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    struct Transform3D {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Quat rotation = MATH::Quat::Identity();
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };

        const Transform3D* parent = nullptr;

        MATH::Mat4 GetLocalMatrix() const;
        MATH::Mat4 GetWorldMatrix() const;
    };

} // namespace HIKARI
