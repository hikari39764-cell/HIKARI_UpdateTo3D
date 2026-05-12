#pragma once

#include <string>
#include <cstdint>
#include <DirectXMath.h>

#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {
    class ModelAsset;

    enum class ModelGeometryDebugMode {
        Normal,
        WireOverlay,
        WireOnly,
    };

    struct ModelRenderItem {
        const ModelAsset* model = nullptr;
        uint64_t instanceKey = 0;
        Transform3D worldTransform;

        std::string materialFxProfileId;
        DirectX::XMFLOAT4 materialFxParamValues[4]{};
        bool materialFxValuesInitialized = false;

        uint32_t postGroupMask = 0;

        std::string animationClipName;
        float animationTimeSec = 0.0f;
        bool animationLoop = true;

        bool showSkeletonDebug = false;
        bool skeletonDebugXRay = false;
        uint32_t skeletonDebugColor = 0x00FFAAFF;

        bool castShadow = true;
        bool receiveShadow = true;
        ModelGeometryDebugMode geometryDebugMode = ModelGeometryDebugMode::Normal;
    };
}
