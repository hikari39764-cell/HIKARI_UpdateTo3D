#pragma once
#include "HIKARI_Math3D.h"

namespace HIKARI {

    struct SceneLighting {
        MATH::Vec3 directionalDir{ 0.4f, 1.0f, -0.6f };
        float directionalIntensity = 1.0f;

        MATH::Vec3 directionalColor{ 1.0f, 1.0f, 1.0f };
        float ambientIntensity = 0.25f;

        MATH::Vec3 ambientColor{ 1.0f, 1.0f, 1.0f };
        float specularIntensity = 0.2f;

        MATH::Vec3 specularColor{ 1.0f, 1.0f, 1.0f };
        float specularPower = 32.0f;
    };

} // namespace HIKARI
