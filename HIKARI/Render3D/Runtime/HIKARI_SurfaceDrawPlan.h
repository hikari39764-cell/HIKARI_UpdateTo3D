#pragma once

#include <cstdint>

#include "Render3D/Runtime/HIKARI_RenderSurfaceContract.h"

namespace HIKARI::RENDER3D::RUNTIME {

    enum class SurfaceDrawCommandPass : uint8_t {
        Forward,
        Shadow,
    };

    enum class SurfaceDrawCommandBackend : uint8_t {
        CpuDirect,
        GpuDriven,
    };

    // CPU 実行と将来の GPU-driven 実行を同じ draw plan 上で表現する。
    struct SurfaceDrawCommand {
        SurfaceDrawCommandPass pass = SurfaceDrawCommandPass::Forward;
        SurfaceDrawCommandBackend backend = SurfaceDrawCommandBackend::CpuDirect;

        uint32_t firstExecutableIndex = 0;
        uint32_t packetCount = 0;
        uint32_t firstPacketIndex = kInvalidRenderSurfaceIndex;

        // GPU scene / cluster range は専用 allocator が確定した段階で埋める。
        uint32_t firstGpuSceneInstanceIndex = kInvalidRenderSurfaceIndex;
        uint32_t gpuSceneInstanceCount = 0;
        uint32_t firstClusterRangeIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterRangeCount = 0;

        uint64_t psoKey = 0;
        uint64_t geometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t modelKey = 0;

        bool singlePacket = false;
        bool transparent = false;
    };

} // namespace HIKARI::RENDER3D::RUNTIME
