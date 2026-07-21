#include "Physics/HIKARI_PhysicsCollisionGeometryStore.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include "Assets/Collision/HIKARI_HcollisionFormat.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"

namespace HIKARI::PHYSICS {
    namespace {

        constexpr float kDegreesToRadians =
            0.01745329251994329577f;

        MATH::Vec3 Absolute(const MATH::Vec3& value) noexcept {
            return {
                std::abs(value.x),
                std::abs(value.y),
                std::abs(value.z)
            };
        }

        MATH::Vec3 Multiply(
            const MATH::Vec3& left,
            const MATH::Vec3& right) noexcept {
            return {
                left.x * right.x,
                left.y * right.y,
                left.z * right.z
            };
        }

        PhysicsShapeType ToRuntimeShapeType(
            ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept {
            switch (type) {
            case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
                return PhysicsShapeType::Sphere;
            case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
                return PhysicsShapeType::Capsule;
            case ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull:
                return PhysicsShapeType::ConvexHull;
            case ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh:
                return PhysicsShapeType::TriangleMesh;
            case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
            default:
                return PhysicsShapeType::Box;
            }
        }

        PhysicsShapeDesc BuildShape(
            const ASSETS::COLLISION::CollisionGeometryShape& source,
            const ColliderComponent& collider,
            const MATH::Vec3& worldScale,
            std::shared_ptr<const PhysicsGeometryBuffer> geometry) {
            const MATH::Vec3 scale = Absolute(worldScale);
            PhysicsShapeDesc shape{};
            shape.type = ToRuntimeShapeType(source.type);
            shape.localCenter = Multiply(source.center, scale);
            shape.localRotation = MATH::NormalizeQ(
                MATH::Quat::FromEulerXYZ(
                    source.rotationEulerDegrees.x * kDegreesToRadians,
                    source.rotationEulerDegrees.y * kDegreesToRadians,
                    source.rotationEulerDegrees.z * kDegreesToRadians));
            shape.halfExtents = Multiply(source.size, scale) * 0.5f;
            const float radialScale = (std::max)(scale.x, scale.z);
            shape.radius = source.radius * radialScale;
            shape.height = source.height * scale.y;
            shape.localScale = scale;
            shape.geometry = std::move(geometry);
            shape.vertexOffset = source.vertexOffset;
            shape.vertexCount = source.vertexCount;
            shape.indexOffset = source.indexOffset;
            shape.indexCount = source.indexCount;
            shape.isTrigger = collider.IsTrigger();
            shape.material = PhysicsMaterialDesc{
                collider.GetFriction(),
                collider.GetRestitution(),
                collider.GetDensity()
            };
            shape.filter = PhysicsCollisionFilter{
                collider.GetCollisionLayer(),
                collider.GetCollisionMask()
            };
            return shape;
        }

    } // namespace

    void PhysicsCollisionGeometryStore::SetAssetDatabase(
        AssetDatabase* assetDatabase) noexcept {
        if (assetDatabase_ == assetDatabase) {
            return;
        }
        assetDatabase_ = assetDatabase;
        Clear();
    }

    void PhysicsCollisionGeometryStore::Clear() noexcept {
        cache_.clear();
    }

    bool PhysicsCollisionGeometryStore::ResolveArtifactPath(
        const std::string& assetId,
        std::filesystem::path& outPath) const {
        if (assetDatabase_ == nullptr || assetId.empty()) {
            return false;
        }
        const AssetRecord* record = assetDatabase_->FindByGuid(
            AssetGuid{ assetId });
        if (record == nullptr) {
            return false;
        }
        const auto found = std::find_if(
            record->artifactManifest.artifacts.begin(),
            record->artifactManifest.artifacts.end(),
            [](const AssetArtifactDesc& artifact) {
                return artifact.role == "CollisionGeometry" ||
                    artifact.format == "HCOLLISION";
            });
        if (found == record->artifactManifest.artifacts.end() ||
            found->path.empty()) {
            return false;
        }
        outPath = std::filesystem::path(found->path);
        if (outPath.is_relative()) {
            outPath = assetDatabase_->GetProjectRoot() / outPath;
        }
        outPath = outPath.lexically_normal();
        return true;
    }

    const PhysicsCollisionGeometryStore::CacheEntry*
        PhysicsCollisionGeometryStore::Load(const std::string& assetId) {
        std::filesystem::path artifactPath{};
        if (!ResolveArtifactPath(assetId, artifactPath)) {
            return nullptr;
        }

        std::error_code ec{};
        const std::filesystem::file_time_type lastWriteTime =
            std::filesystem::last_write_time(artifactPath, ec);
        if (ec) {
            return nullptr;
        }
        const uintmax_t fileSize = std::filesystem::file_size(
            artifactPath,
            ec);
        if (ec) {
            return nullptr;
        }

        auto found = cache_.find(assetId);
        if (found != cache_.end() &&
            found->second.artifactPath == artifactPath &&
            found->second.lastWriteTime == lastWriteTime &&
            found->second.fileSize == fileSize) {
            return &found->second;
        }

        ASSETS::COLLISION::CollisionGeometryAsset asset{};
        std::string message{};
        if (!ASSETS::COLLISION::ReadHcollisionFile(
                artifactPath,
                asset,
                message)) {
            HIKARI_LOG_ERROR(
                "[Physics] failed to load collision geometry: " +
                message);
            cache_.erase(assetId);
            return nullptr;
        }

        CacheEntry& entry = cache_[assetId];
        entry.artifactPath = std::move(artifactPath);
        entry.lastWriteTime = lastWriteTime;
        entry.fileSize = fileSize;
        entry.asset = std::move(asset);
        auto geometry = std::make_shared<PhysicsGeometryBuffer>();
        geometry->vertices = entry.asset.vertices;
        geometry->indices = entry.asset.indices;
        entry.geometry = std::move(geometry);
        return &entry;
    }

    bool PhysicsCollisionGeometryStore::AppendShapes(
        const ColliderComponent& collider,
        const MATH::Vec3& worldScale,
        std::vector<PhysicsShapeDesc>& outShapes) {
        if (!collider.IsEnabled() ||
            !collider.UsesCollisionGeometryAsset()) {
            return false;
        }
        const CacheEntry* entry = Load(
            collider.GetCollisionGeometryAssetId());
        if (entry == nullptr || !entry->asset.IsUsable()) {
            return false;
        }
        outShapes.reserve(outShapes.size() + entry->asset.shapes.size());
        for (const ASSETS::COLLISION::CollisionGeometryShape& source :
                entry->asset.shapes) {
            outShapes.push_back(BuildShape(
                source,
                collider,
                worldScale,
                entry->geometry));
        }
        return true;
    }

} // namespace HIKARI::PHYSICS
