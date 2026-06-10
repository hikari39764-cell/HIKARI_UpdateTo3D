#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceContract.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::RENDER3D::RUNTIME {

    struct SurfaceDrawPacket;

    enum class SurfaceGpuSceneInstanceFlags : uint32_t {
        None = 0,
        StaticGeometry = 1u << 0,
        CastShadow = 1u << 1,
        ReceiveShadow = 1u << 2,
        AlphaMasked = 1u << 3,
        Transparent = 1u << 4,
        MaterialOverride = 1u << 5,
    };

    enum class SurfaceGpuSceneResourceFlags : uint32_t {
        None = 0,
        Mesh = 1u << 0,
        Material = 1u << 1,
        ClusterGeometry = 1u << 2,
    };

    // GPU scene buffer にそのまま並べる packet 由来の instance。
    struct SurfaceGpuSceneInstance {
        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        MATH::Vec4 boundsCenterRadius{};

        uint32_t sourcePacketIndex = kInvalidRenderSurfaceIndex;
        uint32_t sourceSurfaceInstanceIndex = kInvalidRenderSurfaceIndex;
        uint32_t objectIdLow = 0;
        uint32_t objectIdHigh = 0;

        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t sourceMaterialIndex = 0;
        uint32_t nodeIndex = kInvalidRenderSurfaceIndex;

        uint32_t flags = 0;
        uint32_t materialDataIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterRangeIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterRangeCount = 0;

        uint32_t meshResourceIndex = 0;
        uint32_t meshResourceGeneration = 0;
        uint32_t materialResourceIndex = 0;
        uint32_t materialResourceGeneration = 0;

        uint32_t clusterGeometryResourceIndex = 0;
        uint32_t clusterGeometryResourceGeneration = 0;
        uint32_t resourceFlags = 0;
        uint32_t fxFlags = 0;

        MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
    };

    static_assert(sizeof(SurfaceGpuSceneInstance) == 352u);

    struct SurfaceGpuSceneBuildStats {
        uint32_t commandCount = 0;
        uint32_t instanceCount = 0;
        uint32_t skippedInvalidCommandCount = 0;
        uint32_t skippedInvalidPacketCount = 0;
        uint32_t maxCommandInstanceCount = 0;
        uint32_t resourceBackedInstanceCount = 0;
        uint32_t missingResourceHandleInstanceCount = 0;
    };

    class SurfaceGpuSceneWriter final {
    public:
        static SurfaceGpuSceneBuildStats BuildCommandRanges(
            const std::vector<SurfaceDrawPacket>& packets,
            const std::vector<uint32_t>& executablePacketIndices,
            std::vector<SurfaceDrawCommand>& commands,
            std::vector<SurfaceGpuSceneInstance>& outInstances);

    private:
        static SurfaceGpuSceneInstance BuildInstance(
            const SurfaceDrawPacket& packet,
            uint32_t sourcePacketIndex,
            uint32_t sourceCommandLocalIndex);
    };

} // namespace HIKARI::RENDER3D::RUNTIME
