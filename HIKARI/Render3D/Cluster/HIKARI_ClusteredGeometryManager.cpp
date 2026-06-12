#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"

#include <algorithm>

#include "Assets/Geometry/HIKARI_ClusteredGeometryValidator.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        ClusteredGeometryManager gManager{};
    }

    const ClusteredGeometryAsset* ClusteredGeometryManager::LoadOrGet(const std::filesystem::path& path) {
        ++stats_.requestCount;
        const std::string key = MakeKey(path);
        auto it = assets_.find(key);
        if (it != assets_.end()) {
            ++stats_.hitCount;
            return it->second && it->second->valid ? it->second.get() : nullptr;
        }

        ++stats_.missCount;
        auto asset = std::make_unique<ClusteredGeometryAsset>();
        std::string message{};
        if (!ASSETS::GEOMETRY::ReadHcmeshFile(path, *asset, message)) {
            lastMessage_ = message;
            assets_[key] = std::move(asset);
            RebuildStats();
            return nullptr;
        }

        const ASSETS::GEOMETRY::ClusteredGeometryValidationResult validation =
            ASSETS::GEOMETRY::ValidateClusteredGeometryAsset(*asset);
        if (!validation.valid) {
            asset->valid = false;
            lastMessage_ = validation.messages.empty() ? "[HCMESH] validation failed" : validation.messages.front();
            assets_[key] = std::move(asset);
            RebuildStats();
            return nullptr;
        }

        lastMessage_ = message;
        const ClusteredGeometryAsset* raw = asset.get();
        assets_[key] = std::move(asset);
        RebuildStats();
        return raw;
    }

    void ClusteredGeometryManager::Invalidate(const std::filesystem::path& path) {
        assets_.erase(MakeKey(path));
        RebuildStats();
    }

    void ClusteredGeometryManager::Clear() {
        assets_.clear();
        lastMessage_.clear();
        stats_ = {};
    }

    const ClusteredGeometryManagerStats& ClusteredGeometryManager::GetStats() const {
        return stats_;
    }

    const std::string& ClusteredGeometryManager::GetLastMessage() const {
        return lastMessage_;
    }

    std::string ClusteredGeometryManager::MakeKey(const std::filesystem::path& path) const {
        return path.lexically_normal().generic_string();
    }

    void ClusteredGeometryManager::RebuildStats() {
        const uint32_t requests = stats_.requestCount;
        const uint32_t hits = stats_.hitCount;
        const uint32_t misses = stats_.missCount;
        stats_ = {};
        stats_.requestCount = requests;
        stats_.hitCount = hits;
        stats_.missCount = misses;

        for (const auto& pair : assets_) {
            const ClusteredGeometryAsset* asset = pair.second.get();
            if (asset == nullptr || !asset->valid) {
                ++stats_.invalidAssetCount;
                continue;
            }

            ++stats_.validAssetCount;
            stats_.surfaceCount += static_cast<uint32_t>(asset->surfaces.size());
            stats_.surfaceLodRangeCount += static_cast<uint32_t>(asset->surfaceLodRanges.size());
            stats_.clusterCount += static_cast<uint32_t>(asset->clusters.size());
            stats_.pageCount += static_cast<uint32_t>(asset->pages.size());
            stats_.totalTriangleCount += asset->totalTriangleCount;
            stats_.totalVertexCount += asset->totalVertexCount;
            stats_.maxVerticesPerCluster = (std::max)(
                stats_.maxVerticesPerCluster,
                CountMaxClusterVertices(*asset));
            stats_.skippedSkinnedPrimitiveCount += asset->skippedSkinnedPrimitiveCount;
            stats_.skippedMorphPrimitiveCount += asset->skippedMorphPrimitiveCount;
            stats_.skippedInvalidPrimitiveCount += asset->skippedInvalidPrimitiveCount;
            stats_.unsupportedFeatureCount += asset->unsupportedFeatureCount + asset->unsupportedPrimitiveModeCount;
        }
    }

    ClusteredGeometryManager& GetClusteredGeometryManager() {
        return gManager;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
