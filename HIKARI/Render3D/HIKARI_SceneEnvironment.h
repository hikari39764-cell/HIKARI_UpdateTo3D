#pragma once

#include <string>
#include <vector>
#include "HIKARI_Math3D.h"

namespace HIKARI {

    struct AmbientLight {
        MATH::Vec3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 0.25f;
    };

    struct DirectionalLight {
        bool enabled = true;
        MATH::Vec3 direction{ 0.4f, -1.0f, -0.6f };
        float intensity = 1.0f;
        MATH::Vec3 color{ 1.0f, 1.0f, 1.0f };
    };

    struct PointLight {
        bool enabled = true;
        MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
        float range = 5.0f;
        MATH::Vec3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
    };

    struct SkySettings {
        bool enabled = true;
        std::string meshAsset = "SkySphere";
        std::string texturePath = "Assets/Skies/sky_day.png";
        float yaw = 0.0f;
        float exposure = 1.0f;
        MATH::Vec3 tint{ 1.0f, 1.0f, 1.0f };
    };

    struct SceneEnvironment {
        AmbientLight ambient{};
        DirectionalLight directional{};
        std::vector<PointLight> pointLights{};
        SkySettings sky{};
        float specularIntensity = 0.2f;
        float specularPower = 32.0f;
        bool showLightDebug = true;
        bool showPointLightMarkers = true;
        bool showSkyDebugInfo = false;
    };

} // namespace HIKARI
