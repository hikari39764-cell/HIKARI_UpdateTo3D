#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        bool IsFinite(const MATH::Vec3& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool IsShapeUsable(const CollisionGeometryShape& shape) noexcept {
            if (shape.id == 0u ||
                !IsFinite(shape.center) ||
                !IsFinite(shape.rotationEulerDegrees) ||
                !IsFinite(shape.size) ||
                !std::isfinite(shape.radius) ||
                !std::isfinite(shape.height)) {
                return false;
            }
            switch (shape.type) {
            case CollisionGeometryShapeType::Sphere:
                return shape.radius > 0.0f;
            case CollisionGeometryShapeType::Capsule:
                return shape.radius > 0.0f &&
                    shape.height >= shape.radius * 2.0f;
            case CollisionGeometryShapeType::Box:
            default:
                return shape.size.x > 0.0f &&
                    shape.size.y > 0.0f &&
                    shape.size.z > 0.0f;
            }
        }
    }

    bool CollisionGeometryAsset::IsUsable() const noexcept {
        if (version != kCollisionGeometryAssetVersion ||
            sourceAssetGuid.empty() ||
            !BOUNDS::IsUsable(localBounds) ||
            shapes.empty()) {
            return false;
        }
        return std::all_of(
            shapes.begin(),
            shapes.end(),
            IsShapeUsable);
    }

    uint32_t CollisionGeometryAsset::GetShapeCount() const noexcept {
        return static_cast<uint32_t>(shapes.size());
    }

    uint64_t CollisionGeometryAsset::GetPayloadByteSize() const noexcept {
        return static_cast<uint64_t>(shapes.size()) *
            sizeof(CollisionGeometryShape);
    }

    CollisionGeometryValidationResult ValidateCollisionGeometryAsset(
        const CollisionGeometryAsset& asset) {

        CollisionGeometryValidationResult result{};
        if (asset.version != kCollisionGeometryAssetVersion) {
            result.messages.push_back(
                "unsupported collision geometry version");
        }
        if (asset.sourceAssetGuid.empty()) {
            result.messages.push_back(
                "collision geometry has no source model GUID");
        }
        if (!BOUNDS::IsUsable(asset.localBounds)) {
            result.messages.push_back(
                "collision geometry bounds are invalid");
        }
        if (asset.shapes.empty()) {
            result.messages.push_back(
                "collision geometry has no shapes");
        }

        std::unordered_set<uint64_t> ids{};
        for (const CollisionGeometryShape& shape : asset.shapes) {
            if (!IsShapeUsable(shape)) {
                result.messages.push_back(
                    "collision geometry contains an invalid shape");
                break;
            }
            if (!ids.insert(shape.id).second) {
                result.messages.push_back(
                    "collision geometry contains duplicate shape IDs");
                break;
            }
        }

        result.valid = result.messages.empty();
        return result;
    }

} // namespace HIKARI::ASSETS::COLLISION
