#pragma once

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::ASSETS::GEOMETRY {

    struct ClusterCookSettings {
        uint32_t maxTrianglesPerCluster = RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster;
        uint32_t maxVerticesPerCluster = RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster;
        uint32_t maxClustersPerPage = RENDER3D::CLUSTER::kHcmeshMaxClustersPerPage;

        bool buildAdjacency = true;
        bool buildNormalCone = true;
        bool buildClusterPages = true;
        bool buildPackedGeometry = true;
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
