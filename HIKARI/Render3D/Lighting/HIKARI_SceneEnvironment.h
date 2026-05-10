#pragma once

#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Render3D/HIKARI_Math3D.h"

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

    struct DirectionalShadowSettings {
        bool enabled = true;
        uint32_t resolution = 2048;
        float orthoSize = 30.0f;
        float nearPlane = 0.1f;
        float farPlane = 80.0f;
        float depthBias = 0.001f;
        float normalBias = 0.02f;
        float strength = 0.75f;
        bool pcfEnabled = true;
        float pcfRadius = 1.0f;
        bool stabilize = true;
        bool showDebugTexture = false;
        float shadowDistance = 30.0f;
        bool showDebugFrustum = false;
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
        std::string skyAsset = "DefaultSky";
        float scale = 1.0f;
        float yaw = 0.0f;
        float exposure = 1.0f;
        MATH::Vec3 tint{ 1.0f, 1.0f, 1.0f };
        bool followCamera = true;
    };


    struct ScenePostSettings {
        bool enabled = false;
        std::string globalPostProfileId{};
        DirectX::XMFLOAT4 paramValues[16]{};
        bool valuesInitialized = false;
    };

    struct SceneEnvironment {
        AmbientLight ambient{};
        DirectionalLight directional{};
        DirectionalShadowSettings directionalShadow{};
        std::vector<PointLight> pointLights{};
        SkySettings sky{};
        float specularIntensity = 0.2f;
        float specularPower = 32.0f;
        bool showLightDebug = true;
        bool showPointLightMarkers = true;
        bool showSkyDebugInfo = false;
        ScenePostSettings post{};
    };

} // namespace HIKARI
