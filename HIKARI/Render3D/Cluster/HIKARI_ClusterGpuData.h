#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::CLUSTER {

    constexpr uint32_t kClusterGeometryGpuMagic = 0x534c4348u; // HCLS
    constexpr uint32_t kClusterGeometryGpuVersion = 2u;
    constexpr uint32_t kClusterGeometryGpuSectionAlignment = 16u;

    struct ClusterGeometryGpuHeader {
        uint32_t magic = kClusterGeometryGpuMagic;
        uint32_t version = kClusterGeometryGpuVersion;
        uint32_t flags = 0;
        uint32_t byteSize = 0;

        uint32_t surfaceCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t vertexCount = 0;

        uint32_t indexCount = 0;
        uint32_t materialSlotCount = 0;
        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;

        uint32_t surfaceOffsetBytes = 0;
        uint32_t clusterOffsetBytes = 0;
        uint32_t pageOffsetBytes = 0;
        uint32_t vertexOffsetBytes = 0;

        uint32_t indexOffsetBytes = 0;
        uint32_t materialSlotOffsetBytes = 0;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;

        MATH::Vec4 localBoundsMin{};
        MATH::Vec4 localBoundsMax{};
    };

    struct ClusterGeometryGpuSurface {
        uint32_t nodeIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;
        uint32_t materialIndex = 0;

        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t flags = 0;
        uint32_t firstPage = 0;

        uint32_t pageCount = 0;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;
        uint32_t reserved2 = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
    };

    struct ClusterGeometryGpuCluster {
        uint32_t surfaceIndex = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t firstVertex = 0;

        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        uint32_t flags = 0;
        uint32_t reserved0 = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
        MATH::Vec4 sphereCenterRadius{};
        MATH::Vec4 coneAxisCutoff{};
    };

    struct ClusterGeometryGpuPage {
        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
    };

    struct ClusterGeometryGpuVertex {
        MATH::Vec4 position{};
        MATH::Vec4 normal{};
        MATH::Vec4 tangent{};
        MATH::Vec4 uv01{};
        MATH::Vec4 color{};
    };

    struct ClusterGeometrySurfaceRange {
        uint32_t surfaceIndex = 0;
        uint32_t nodeIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;

        uint32_t materialIndex = 0;
        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;
        uint32_t flags = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        uint32_t firstPage = 0;
        uint32_t pageCount = 0;
    };

    static_assert((sizeof(ClusterGeometryGpuHeader) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuSurface) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuCluster) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuPage) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuVertex) % kClusterGeometryGpuSectionAlignment) == 0);

} // namespace HIKARI::RENDER3D::CLUSTER
