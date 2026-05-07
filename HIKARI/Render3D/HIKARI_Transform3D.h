#pragma once
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    struct Transform3D {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Quat rotation = MATH::Quat::Identity();
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };

        const Transform3D* parent = nullptr;

        // For glTF node hierarchy and future animation/skinning paths.
        // When this flag is true, explicitMatrix is treated as this transform's final world matrix.
        // The TRS fields are still kept for legacy paths and simple editor display.
        bool useExplicitMatrix = false;
        MATH::Mat4 explicitMatrix{};

        MATH::Mat4 GetLocalMatrix() const;
        MATH::Mat4 GetWorldMatrix() const;
    };

} // namespace HIKARI
