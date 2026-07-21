#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"
#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI {
    class AssetDatabase;
    class ColliderComponent;
}

namespace HIKARI::PHYSICS {

    // Resolves HIKARI collision artifacts into backend-neutral runtime shapes.
    // Vendor-specific cooked data never crosses this boundary.
    class PhysicsCollisionGeometryStore {
    public:
        void SetAssetDatabase(AssetDatabase* assetDatabase) noexcept;
        void Clear() noexcept;

        bool AppendShapes(
            const ColliderComponent& collider,
            const MATH::Vec3& worldScale,
            std::vector<PhysicsShapeDesc>& outShapes);

    private:
        struct CacheEntry {
            std::filesystem::path artifactPath{};
            std::filesystem::file_time_type lastWriteTime{};
            uintmax_t fileSize = 0;
            ASSETS::COLLISION::CollisionGeometryAsset asset{};
            std::shared_ptr<const PhysicsGeometryBuffer> geometry{};
        };

        bool ResolveArtifactPath(
            const std::string& assetId,
            std::filesystem::path& outPath) const;
        const CacheEntry* Load(
            const std::string& assetId);

        AssetDatabase* assetDatabase_ = nullptr;
        std::unordered_map<std::string, CacheEntry> cache_{};
    };

} // namespace HIKARI::PHYSICS
