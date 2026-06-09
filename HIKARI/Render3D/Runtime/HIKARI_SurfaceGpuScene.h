#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceContract.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"

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

    // GPU scene buffer にそのまま並べるための packet 由来 instance。
    struct SurfaceGpuSceneInstance {
        MATH::Mat4 world{};
        MATH::Vec4 boundsCenterRadius{};

        uint32_t sourcePacketIndex = kInvalidRenderSurfaceIndex;
        uint32_t sourceSurfaceInstanceIndex = kInvalidRenderSurfaceIndex;
        uint32_t objectIdLow = 0;
        uint32_t objectIdHigh = 0;

        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t materialIndex = 0;
        uint32_t nodeIndex = kInvalidRenderSurfaceIndex;

        uint32_t flags = 0;
        uint32_t commandLocalIndex = 0;
        uint32_t clusterRangeIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterRangeCount = 0;
    };

    static_assert(sizeof(SurfaceGpuSceneInstance) == 128u);

    struct SurfaceGpuSceneBuildStats {
        uint32_t commandCount = 0;
        uint32_t instanceCount = 0;
        uint32_t skippedInvalidCommandCount = 0;
        uint32_t skippedInvalidPacketCount = 0;
        uint32_t maxCommandInstanceCount = 0;
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
            uint32_t commandLocalIndex);
    };

} // namespace HIKARI::RENDER3D::RUNTIME
