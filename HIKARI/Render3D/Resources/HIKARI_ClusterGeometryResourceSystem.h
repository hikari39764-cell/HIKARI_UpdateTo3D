#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuData.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D {

    struct ClusterGeometryGpuLayout {
        uint32_t surfaceCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t meshletPrimitiveCount = 0;
        uint32_t materialSlotCount = 0;

        uint32_t surfaceOffsetBytes = 0;
        uint32_t clusterOffsetBytes = 0;
        uint32_t pageOffsetBytes = 0;
        uint32_t vertexOffsetBytes = 0;
        uint32_t indexOffsetBytes = 0;
        uint32_t meshletPrimitiveOffsetBytes = 0;
        uint32_t materialSlotOffsetBytes = 0;

        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;
        uint32_t flags = 0;
        uint32_t byteSize = 0;

        MATH::Vec4 localBoundsMin{};
        MATH::Vec4 localBoundsMax{};
    };

    struct ClusterGeometryResourceRecord {
        ClusterGeometryResourceHandle handle{};
        std::string sourceKey{};
        std::filesystem::path sourcePath{};
        ClusterGeometryGpuLayout layout{};
        std::vector<CLUSTER::ClusterGeometrySurfaceRange> surfaceRanges{};
        RenderResourceView srv{};
        bool ready = false;
    };

    struct ClusterGeometryResourceSystemStats {
        bool initialized = false;
        uint32_t requestCount = 0;
        uint32_t hitCount = 0;
        uint32_t missCount = 0;
        uint32_t loadedCount = 0;
        uint32_t failedCount = 0;
        uint32_t missingDeviceCount = 0;
        uint32_t resourceCount = 0;
        uint32_t readyResourceCount = 0;
        uint32_t shaderVisibleResourceCount = 0;
        uint32_t missingDescriptorCount = 0;
        uint32_t descriptorAllocationFailedCount = 0;
        uint32_t upgradedVirtualHandleCount = 0;
        uint32_t surfaceCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t meshletPrimitiveCount = 0;
        uint32_t surfaceRangeCount = 0;
        uint64_t gpuBufferBytes = 0;
    };

    void UpdateClusterGeometryResourceContext(const GFX::Context& ctx);
    void ShutdownClusterGeometryResourceSystem();

    ClusterGeometryResourceHandle LoadClusterGeometryResource(
        const std::string& sourceKey,
        const std::filesystem::path& hcmeshPath);

    const ClusterGeometryResourceRecord* GetClusterGeometryResource(
        ClusterGeometryResourceHandle handle);

    const CLUSTER::ClusterGeometrySurfaceRange* FindClusterGeometrySurfaceRange(
        ClusterGeometryResourceHandle handle,
        uint32_t nodeIndex,
        uint32_t meshIndex,
        uint32_t primitiveIndex);

    ClusterGeometryResourceSystemStats GetClusterGeometryResourceSystemStats();

} // namespace HIKARI::RENDER3D
