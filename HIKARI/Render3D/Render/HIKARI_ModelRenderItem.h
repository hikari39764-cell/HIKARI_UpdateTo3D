#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {
    class ModelAsset;
    class Material;

    enum class ModelGeometryDebugMode {
        Normal,
        WireOverlay,
        WireOnly,
    };

    struct ModelPrimitiveDrawKey {
        uint32_t nodeIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;

        bool operator==(const ModelPrimitiveDrawKey& rhs) const {
            return
                nodeIndex == rhs.nodeIndex &&
                meshIndex == rhs.meshIndex &&
                primitiveIndex == rhs.primitiveIndex;
        }
    };

    struct ModelRenderItem {
        const ModelAsset* model = nullptr;
        uint64_t instanceKey = 0;
        Transform3D worldTransform;
        const Material* materialOverride = nullptr;

        std::string materialFxProfileId;
        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
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
        bool submitForward = true;
        bool submitShadow = true;
        std::vector<ModelPrimitiveDrawKey> forwardPrimitiveExclusions{};
        ModelGeometryDebugMode geometryDebugMode = ModelGeometryDebugMode::Normal;
    };
}
