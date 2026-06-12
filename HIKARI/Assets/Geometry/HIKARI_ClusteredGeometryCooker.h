#pragma once

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::ASSETS::GEOMETRY {

    struct ClusterCookSettings {
        uint32_t maxTrianglesPerCluster = RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster;
        uint32_t maxVerticesPerCluster = RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster;
        uint32_t maxClustersPerPage = RENDER3D::CLUSTER::kHcmeshMaxClustersPerPage;
        uint32_t maxSurfaceLodCount = 5;

        // LOD は前段から次段へ連鎖生成する。ratio は前段に対する残存率。
        float lod1TriangleRatio = 0.78f;
        float lod2TriangleRatio = 0.72f;
        float lod3TriangleRatio = 0.66f;
        float lod4TriangleRatio = 0.60f;
        float lod1TargetError = 0.004f;
        float lod2TargetError = 0.008f;
        float lod3TargetError = 0.016f;
        float lod4TargetError = 0.032f;
        float lod0MinScreenRadius = 0.20f;
        float lod1MinScreenRadius = 0.105f;
        float lod2MinScreenRadius = 0.052f;
        float lod3MinScreenRadius = 0.026f;
        float lod4MinScreenRadius = 0.0f;

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
