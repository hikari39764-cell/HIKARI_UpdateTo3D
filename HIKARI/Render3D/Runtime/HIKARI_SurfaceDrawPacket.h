#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::RUNTIME {

    enum class SurfaceDrawPacketPassFlags : uint8_t {
        None = 0,
        Forward = 1u << 0,
        Shadow = 1u << 1,
    };

    struct SurfaceDrawPacketKey {
        uint32_t materialIndex = 0;
        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t passMask = 0;
        bool hasMaterialOverride = false;
        bool skinned = false;
        SurfaceGeometryBackend geometryBackend = SurfaceGeometryBackend::TriangleMesh;

        uint64_t modelKey = 0;
        uint64_t geometryKey = 0;
        uint64_t clusterGeometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t shaderKey = 0;
        uint64_t psoKey = 0;
        uint64_t sortKey = 0;
        SurfaceResourceIds resources{};

        bool resourceKeyValid = false;
        bool objectDataCompatible = false;
        bool clusterMainlineEligible = false;
        bool materialFx = false;
        bool waterMaterialFx = false;
        bool depthAware = false;
        bool alphaMasked = false;
        bool transparent = false;
        bool doubleSided = false;
    };

    inline SurfaceDrawBatchKey BuildSurfaceDrawBatchKey(
        SurfaceDrawCommandPass pass,
        const SurfaceDrawPacketKey& key) {

        SurfaceDrawBatchKey batchKey{};
        batchKey.pass = pass;
        batchKey.geometryBackend = key.geometryBackend;
        batchKey.psoKey = key.psoKey;
        batchKey.geometryKey = key.geometryKey;
        batchKey.transparent = key.transparent;
        batchKey.clusterMainlineEligible = key.clusterMainlineEligible;
        return batchKey;
    }

    struct SurfaceDrawPacket {
        SceneRenderObjectId objectId{};
        uint64_t objectVersion = 0;
        uint32_t sourceSurfaceInstanceIndex = kInvalidRenderSurfaceIndex;

        const SceneSurfaceInstance* sourceSurface = nullptr;
        const ModelAsset* model = nullptr;
        const RenderModelAsset* renderModel = nullptr;
        const RenderSurfaceRecord* surface = nullptr;

        uint32_t surfaceIndex = kInvalidRenderSurfaceIndex;
        uint32_t nodeIndex = kInvalidRenderSurfaceIndex;
        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t materialIndex = 0;

        Transform3D objectWorldTransform{};
        MATH::Mat4 drawWorldMatrix{};
        bool hasDrawWorldMatrix = false;
        Bounds worldBounds{};

        bool valid = false;
        bool visible = true;
        bool isStatic = false;
        bool hasRuntimeAnimation = false;
        std::string animationClipName{};
        float animationTimeSec = 0.0f;
        bool animationLoop = true;
        bool hasSpecialRenderDebug = false;
        bool skinned = false;
        bool castShadow = true;
        bool receiveShadow = true;
        bool forwardCandidate = false;
        bool shadowCandidate = false;
        std::string clusteredGeometryPath{};

        const Material* materialOverride = nullptr;
        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;
        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized = false;

        SurfaceDrawPacketKey key{};
    };

    struct SurfaceDrawPacketValidationResult {
        bool invalidSource = false;
        bool invalidModel = false;
        bool unsupportedGeometry = false;
        bool missingDrawMatrix = false;
        bool invalidBounds = false;
        bool invalidPrimitiveIndex = false;

        bool IsValid() const {
            return
                !invalidSource &&
                !invalidModel &&
                !unsupportedGeometry &&
                !missingDrawMatrix &&
                !invalidBounds &&
                !invalidPrimitiveIndex;
        }
    };

} // namespace HIKARI::RENDER3D::RUNTIME
