#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI::RENDER3D::CLUSTER {

    constexpr uint32_t kInvalidClusterIndex = (std::numeric_limits<uint32_t>::max)();
    constexpr uint32_t kHcmeshMaxClustersPerPage = 64u;

    enum class ClusterSurfaceFlags : uint32_t {
        None = 0,
        Opaque = 1u << 0,
        AlphaMask = 1u << 1,
        Transparent = 1u << 2,
        DoubleSided = 1u << 3,
        Skinned = 1u << 4,
        Unsupported = 1u << 5,
    };

    inline uint32_t ToBits(ClusterSurfaceFlags value) {
        return static_cast<uint32_t>(value);
    }

    inline bool HasFlag(uint32_t flags, ClusterSurfaceFlags value) {
        return (flags & ToBits(value)) != 0u;
    }

    inline void AddFlag(uint32_t& flags, ClusterSurfaceFlags value) {
        flags |= ToBits(value);
    }

    struct ClusterVertex {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
        MATH::Vec2 uv0{};
        MATH::Vec2 uv1{};
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct ClusterSurface {
        uint32_t nodeIndex = kInvalidClusterIndex;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;
        uint32_t materialIndex = 0;

        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        Bounds localBounds{};

        uint32_t flags = 0;
    };

    struct MeshCluster {
        uint32_t surfaceIndex = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        uint32_t triangleCount = 0;

        Bounds localBounds{};

        MATH::Vec3 sphereCenter{};
        float sphereRadius = 0.0f;

        MATH::Vec3 coneAxis{ 0.0f, 1.0f, 0.0f };
        float coneCutoff = 0.0f;

        uint32_t flags = 0;
    };

    struct ClusterPage {
        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        Bounds localBounds{};
    };

    struct ClusteredGeometryAsset {
        AssetGuid sourceModelGuid{};
        std::string sourceModelPath{};

        std::vector<ClusterSurface> surfaces{};
        std::vector<MeshCluster> clusters{};
        std::vector<ClusterPage> pages{};

        std::vector<ClusterVertex> packedVertices{};
        std::vector<uint32_t> packedIndices{};
        std::vector<uint32_t> materialSlotMapping{};

        Bounds localBounds{};

        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;

        uint32_t skippedPrimitiveCount = 0;
        uint32_t skippedSkinnedPrimitiveCount = 0;
        uint32_t skippedMorphPrimitiveCount = 0;
        uint32_t skippedInvalidPrimitiveCount = 0;
        uint32_t unsupportedPrimitiveModeCount = 0;
        uint32_t unsupportedFeatureCount = 0;

        bool valid = false;
    };

    struct ClusteredGeometryBuildReport {
        uint32_t surfaceCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t triangleCount = 0;
        uint32_t vertexCount = 0;
        uint32_t maxVerticesPerCluster = 0;
        uint32_t skippedSkinnedPrimitiveCount = 0;
        uint32_t skippedMorphPrimitiveCount = 0;
        uint32_t skippedInvalidPrimitiveCount = 0;
        uint32_t unsupportedPrimitiveModeCount = 0;
        uint32_t unsupportedFeatureCount = 0;
        std::vector<std::string> messages{};
    };

    uint32_t CountClusterTriangles(const ClusteredGeometryAsset& asset);
    uint32_t CountMaxClusterVertices(const ClusteredGeometryAsset& asset);

} // namespace HIKARI::RENDER3D::CLUSTER
