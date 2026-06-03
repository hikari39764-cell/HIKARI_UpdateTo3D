#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

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

        uint64_t modelKey = 0;
        uint64_t geometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t shaderKey = 0;
        uint64_t psoKey = 0;
        uint64_t sortKey = 0;

        bool resourceKeyValid = false;
        bool alphaMasked = false;
        bool transparent = false;
    };

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
        bool skinned = false;
        bool castShadow = true;
        bool receiveShadow = true;
        bool forwardCandidate = false;
        bool shadowCandidate = false;

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

    class SurfaceDrawPacketBuilder {
    public:
        struct Stats {
            uint32_t packetCount = 0;
            uint32_t validPacketCount = 0;
            uint32_t invalidPacketCount = 0;

            uint32_t visiblePacketCount = 0;
            uint32_t hiddenPacketCount = 0;
            uint32_t staticPacketCount = 0;
            uint32_t dynamicPacketCount = 0;
            uint32_t staticGeometryPacketCount = 0;
            uint32_t skinnedPacketCount = 0;

            uint32_t forwardCandidateCount = 0;
            uint32_t shadowCandidateCount = 0;

            uint32_t materialAssetPacketCount = 0;
            uint32_t materialOverridePacketCount = 0;
            uint32_t opaquePacketCount = 0;
            uint32_t alphaMaskedPacketCount = 0;
            uint32_t transparentPacketCount = 0;

            uint32_t modelBucketCount = 0;
            uint32_t geometryBucketCount = 0;
            uint32_t materialBucketCount = 0;
            uint32_t textureSetBucketCount = 0;
            uint32_t shaderBucketCount = 0;
            uint32_t psoBucketCount = 0;
            uint32_t sortOrderBreakCount = 0;
            uint32_t invalidResourceKeyCount = 0;

            uint32_t invalidSourceCount = 0;
            uint32_t invalidModelCount = 0;
            uint32_t unsupportedGeometryCount = 0;
            uint32_t missingDrawMatrixCount = 0;
            uint32_t invalidBoundsCount = 0;
            uint32_t invalidPrimitiveIndexCount = 0;
        };

        void Clear();
        void BuildFromSceneRenderCache(const SceneRenderCache& sceneCache);

        const std::vector<SurfaceDrawPacket>& GetPackets() const;
        const Stats& GetStats() const;

        static SurfaceDrawPacketValidationResult ValidatePacket(const SurfaceDrawPacket& packet);

    private:
        void AppendPacket(const SceneSurfaceInstance& surfaceInstance, uint32_t sourceSurfaceInstanceIndex);
        void RefreshStats();

        std::vector<SurfaceDrawPacket> packets_{};
        Stats stats_{};
    };

} // namespace HIKARI::RENDER3D::RUNTIME
