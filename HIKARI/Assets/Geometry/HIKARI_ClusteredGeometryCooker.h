#pragma once

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::ASSETS::GEOMETRY {

    enum class SurfacePartitionPolicy {
        Disabled,
        SceneStatic,
        CharacterStatic,
    };

    struct ClusterCookSettings {
        uint32_t maxTrianglesPerCluster = RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster;
        uint32_t minTrianglesPerCluster = 32;
        uint32_t maxVerticesPerCluster = RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster;
        uint32_t maxClustersPerPage = RENDER3D::CLUSTER::kHcmeshMaxClustersPerPage;
        uint32_t maxSurfaceLodCount = 5;

        // LOD は前段から次段へ連鎖生成する。ratio は前段に対する残存率。
        float lod1TriangleRatio = 0.60f;
        float lod2TriangleRatio = 0.50f;
        float lod3TriangleRatio = 0.40f;
        float lod4TriangleRatio = 0.30f;
        float lod1TargetError = 0.006f;
        float lod2TargetError = 0.014f;
        float lod3TargetError = 0.030f;
        float lod4TargetError = 0.060f;
        float lod0MinScreenRadius = 0.18f;
        float lod1MinScreenRadius = 0.085f;
        float lod2MinScreenRadius = 0.040f;
        float lod3MinScreenRadius = 0.018f;
        float lod4MinScreenRadius = 0.0f;

        // 大きな static opaque surface は cook 時に section 化し、LOD と culling の判定単位を小さくする。
        SurfacePartitionPolicy surfacePartitionPolicy = SurfacePartitionPolicy::SceneStatic;
        bool partitionLargeStaticSurfaces = true;
        uint32_t largeSurfacePartitionMinTriangles = 2048;
        uint32_t largeSurfacePartitionMinTrianglesPerChunk = 512;
        uint32_t largeSurfacePartitionMaxDepth = 4;
        float largeSurfacePartitionMaxExtent = 6.0f;
        bool lockPartitionBorders = true;

        // meshoptimizer の meshlet builder 用。小さすぎる cluster を避けつつ cone culling 用の局所性も残す。
        float meshletConeWeight = 0.35f;
        float meshletSplitFactor = 2.0f;

        bool buildAdjacency = true;
        bool buildNormalCone = true;
        bool buildClusterPages = true;
        bool buildPackedGeometry = true;
        bool buildSurfaceLods = true;
        bool generateMissingNormals = true;
        bool generateMissingTangents = true;
    };

    bool CookClusteredGeometryFromModel(
        const ModelAsset& model,
        const AssetGuid& sourceGuid,
        const ClusterCookSettings& settings,
        RENDER3D::CLUSTER::ClusteredGeometryAsset& outAsset,
        RENDER3D::CLUSTER::ClusteredGeometryBuildReport& outReport);

} // namespace HIKARI::ASSETS::GEOMETRY
