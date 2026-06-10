#pragma once

#include <cstdint>

#include "Render3D/Runtime/HIKARI_RenderSurfaceContract.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::RENDER3D::RUNTIME {

    enum class SurfaceDrawCommandPass : uint8_t {
        Forward,
        DepthAware,
        Shadow,
    };

    enum class SurfaceDrawCommandBackend : uint8_t {
        CpuDirect,
        GpuDriven,
    };

    // 将来の resource pool / GPU culling が参照する surface 単位の資源 ID。
    struct SurfaceResourceIds {
        MeshResourceHandle mesh{};
        MaterialResourceHandle material{};
        ClusterGeometryResourceHandle clusterGeometry{};

        uint64_t modelKey = 0;
        uint64_t geometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t shaderKey = 0;
        uint64_t pipelineKey = 0;

        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t materialIndex = 0;

        constexpr bool HasStableKeys() const {
            return modelKey != 0 && geometryKey != 0 && materialKey != 0;
        }

        constexpr bool HasPoolHandles() const {
            return mesh.IsValid() && material.IsValid();
        }
    };

    // draw command をまとめる最小単位。GPU-driven 化しても indirect command の境界に使う。
    struct SurfaceDrawBatchKey {
        SurfaceDrawCommandPass pass = SurfaceDrawCommandPass::Forward;
        uint64_t psoKey = 0;
        uint64_t geometryKey = 0;
        bool transparent = false;
    };

    inline bool IsValidSurfaceDrawBatchKey(const SurfaceDrawBatchKey& key) {
        return key.psoKey != 0 && key.geometryKey != 0;
    }

    inline bool IsSameSurfaceDrawBatchKey(
        const SurfaceDrawBatchKey& lhs,
        const SurfaceDrawBatchKey& rhs) {

        return
            lhs.pass == rhs.pass &&
            lhs.psoKey == rhs.psoKey &&
            lhs.geometryKey == rhs.geometryKey &&
            lhs.transparent == rhs.transparent;
    }

    // D3D12_DRAW_INDEXED_ARGUMENTS と同じ意味を持つ、backend 非依存の draw args。
    struct SurfaceDrawIndexedArgs {
        uint32_t indexCountPerInstance = 0;
        uint32_t instanceCount = 0;
        uint32_t startIndexLocation = 0;
        int32_t baseVertexLocation = 0;
        uint32_t startInstanceLocation = 0;
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

        SurfaceDrawBatchKey batchKey{};
        SurfaceResourceIds resources{};
        SurfaceDrawIndexedArgs drawArgs{};
        uint64_t psoKey = 0;
        uint64_t geometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t modelKey = 0;

        bool singlePacket = false;
        bool transparent = false;
        bool drawArgsValid = false;
    };

} // namespace HIKARI::RENDER3D::RUNTIME
