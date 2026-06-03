#include "Assets/Geometry/HIKARI_ClusteredGeometryValidator.h"

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::GEOMETRY {

    namespace {
        bool RangeValid(uint32_t first, uint32_t count, size_t size) {
            const uint64_t begin = first;
            const uint64_t end = begin + count;
            return end <= static_cast<uint64_t>(size);
        }

        void AddMessage(
            ClusteredGeometryValidationResult& result,
            const std::string& message) {
            if (result.messages.size() < 32u) {
                result.messages.push_back(message);
            }
        }
    }

    ClusteredGeometryValidationResult ValidateClusteredGeometryAsset(
        const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset) {

        ClusteredGeometryValidationResult result{};

        if (asset.surfaces.empty()) {
            AddMessage(result, "no surfaces");
        }
        if (asset.clusters.empty()) {
            AddMessage(result, "no clusters");
        }
        if (asset.packedVertices.empty()) {
            AddMessage(result, "no packed vertices");
        }
        if (asset.packedIndices.empty()) {
            AddMessage(result, "no packed indices");
        }

        for (size_t i = 0; i < asset.surfaces.size(); ++i) {
            const RENDER3D::CLUSTER::ClusterSurface& surface = asset.surfaces[i];
            bool surfaceValid = true;
            if (!RangeValid(surface.firstCluster, surface.clusterCount, asset.clusters.size()) ||
                !RangeValid(surface.firstIndex, surface.indexCount, asset.packedIndices.size()) ||
                !RangeValid(surface.firstVertex, surface.vertexCount, asset.packedVertices.size())) {
                surfaceValid = false;
            }
            if (surface.materialIndex >= asset.materialSlotMapping.size()) {
                ++result.invalidMaterialCount;
                surfaceValid = false;
            }
            if (!BOUNDS::IsUsable(surface.localBounds)) {
                ++result.invalidBoundsCount;
                surfaceValid = false;
            }
            if (!surfaceValid) {
                ++result.invalidSurfaceCount;
                AddMessage(result, "invalid surface " + std::to_string(i));
            }
        }

        for (size_t i = 0; i < asset.clusters.size(); ++i) {
            const RENDER3D::CLUSTER::MeshCluster& cluster = asset.clusters[i];
            bool clusterValid = true;
            if (cluster.surfaceIndex >= asset.surfaces.size() ||
                cluster.triangleCount == 0u ||
                cluster.indexCount != cluster.triangleCount * 3u ||
                !RangeValid(cluster.firstIndex, cluster.indexCount, asset.packedIndices.size()) ||
                !RangeValid(cluster.firstVertex, cluster.vertexCount, asset.packedVertices.size())) {
                clusterValid = false;
            }
            if (!BOUNDS::IsUsable(cluster.localBounds) || cluster.sphereRadius <= 0.0f) {
                ++result.invalidBoundsCount;
                clusterValid = false;
            }
            if (!clusterValid) {
                ++result.invalidClusterCount;
                AddMessage(result, "invalid cluster " + std::to_string(i));
            }
        }

        for (size_t i = 0; i < asset.pages.size(); ++i) {
            const RENDER3D::CLUSTER::ClusterPage& page = asset.pages[i];
            bool pageValid = true;
            if (page.clusterCount == 0u ||
                page.clusterCount > RENDER3D::CLUSTER::kHcmeshMaxClustersPerPage ||
                !RangeValid(page.firstCluster, page.clusterCount, asset.clusters.size()) ||
                !RangeValid(page.firstIndex, page.indexCount, asset.packedIndices.size()) ||
                !RangeValid(page.firstVertex, page.vertexCount, asset.packedVertices.size())) {
                pageValid = false;
            }
            if (!BOUNDS::IsUsable(page.localBounds)) {
                ++result.invalidBoundsCount;
                pageValid = false;
            }
            if (!pageValid) {
                ++result.invalidPageCount;
                AddMessage(result, "invalid page " + std::to_string(i));
            }
        }

        result.valid =
            asset.valid &&
            !asset.surfaces.empty() &&
            !asset.clusters.empty() &&
            !asset.packedVertices.empty() &&
            !asset.packedIndices.empty() &&
            result.invalidSurfaceCount == 0u &&
            result.invalidClusterCount == 0u &&
            result.invalidPageCount == 0u &&
            result.invalidMaterialCount == 0u;

        return result;
    }

} // namespace HIKARI::ASSETS::GEOMETRY
