#include "Editor/Workspaces/HIKARI_ModelCollisionWorkspaceInteraction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::EDITOR {
    namespace {
        constexpr float kDegreesToRadians =
            0.01745329251994329577f;

        bool Unproject(
            const MATH::Mat4& inverseViewProjection,
            float x,
            float y,
            float z,
            MATH::Vec3& outPoint) noexcept {

            const MATH::Vec4 world = inverseViewProjection.TransformPoint({
                x,
                y,
                z,
                1.0f
            });
            if (!std::isfinite(world.x) ||
                !std::isfinite(world.y) ||
                !std::isfinite(world.z) ||
                !std::isfinite(world.w) ||
                std::abs(world.w) <= 1.0e-6f) {
                return false;
            }
            const float inverseW = 1.0f / world.w;
            outPoint = {
                world.x * inverseW,
                world.y * inverseW,
                world.z * inverseW
            };
            return BOUNDS::IsFinite(outPoint);
        }

        bool IntersectRayBounds(
            const ModelCollisionPointerRay& ray,
            const Bounds& bounds,
            float& outDistance) noexcept {

            if (!BOUNDS::IsUsable(bounds)) {
                return false;
            }
            float minimumDistance = 0.0f;
            float maximumDistance =
                (std::numeric_limits<float>::max)();
            const std::array<float, 3> origins{
                ray.origin.x, ray.origin.y, ray.origin.z
            };
            const std::array<float, 3> directions{
                ray.direction.x, ray.direction.y, ray.direction.z
            };
            const std::array<float, 3> minimums{
                bounds.min.x, bounds.min.y, bounds.min.z
            };
            const std::array<float, 3> maximums{
                bounds.max.x, bounds.max.y, bounds.max.z
            };
            for (size_t axis = 0; axis < 3u; ++axis) {
                if (std::abs(directions[axis]) <= 1.0e-7f) {
                    if (origins[axis] < minimums[axis] ||
                        origins[axis] > maximums[axis]) {
                        return false;
                    }
                    continue;
                }
                const float inverseDirection = 1.0f / directions[axis];
                float first =
                    (minimums[axis] - origins[axis]) * inverseDirection;
                float second =
                    (maximums[axis] - origins[axis]) * inverseDirection;
                if (first > second) {
                    std::swap(first, second);
                }
                minimumDistance = (std::max)(minimumDistance, first);
                maximumDistance = (std::min)(maximumDistance, second);
                if (minimumDistance > maximumDistance) {
                    return false;
                }
            }
            outDistance = minimumDistance;
            return std::isfinite(outDistance);
        }

        float BoundsVolume(const Bounds& bounds) noexcept {
            const MATH::Vec3 size = bounds.max - bounds.min;
            return (std::max)(
                0.0f,
                size.x * size.y * size.z);
        }
    }

    bool BuildModelCollisionPointerRay(
        const Camera3D& camera,
        float normalizedViewportX,
        float normalizedViewportY,
        ModelCollisionPointerRay& outRay) noexcept {

        const float clipX = normalizedViewportX * 2.0f - 1.0f;
        const float clipY = 1.0f - normalizedViewportY * 2.0f;
        const MATH::Mat4 inverseViewProjection =
            MATH::Inverse(camera.GetViewProj());
        MATH::Vec3 nearPoint{};
        MATH::Vec3 farPoint{};
        if (!Unproject(
                inverseViewProjection,
                clipX,
                clipY,
                0.0f,
                nearPoint) ||
            !Unproject(
                inverseViewProjection,
                clipX,
                clipY,
                1.0f,
                farPoint)) {
            return false;
        }
        const MATH::Vec3 direction = farPoint - nearPoint;
        if (MATH::Length(direction) <= 1.0e-6f) {
            return false;
        }
        outRay.origin = nearPoint;
        outRay.direction = MATH::Normalize(direction);
        return true;
    }

    Bounds ComputeModelCollisionShapeBounds(
        const ASSETS::COLLISION::ModelCollisionShape& shape) noexcept {

        MATH::Vec3 half{};
        switch (shape.type) {
        case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
            half = { shape.radius, shape.radius, shape.radius };
            break;
        case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
            half = {
                shape.radius,
                shape.height * 0.5f,
                shape.radius
            };
            break;
        case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
        default:
            half = shape.size * 0.5f;
            break;
        }
        const Bounds localBounds{ half * -1.0f, half };
        const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
            shape.rotationEulerDegrees.x * kDegreesToRadians,
            shape.rotationEulerDegrees.y * kDegreesToRadians,
            shape.rotationEulerDegrees.z * kDegreesToRadians);
        return BOUNDS::TransformBounds(
            localBounds,
            MATH::Mat4::TRS(
                shape.center,
                rotation,
                { 1.0f, 1.0f, 1.0f }));
    }

    uint64_t PickModelCollisionShape(
        const ModelCollisionPointerRay& ray,
        const ASSETS::COLLISION::ModelCollisionSetup& setup,
        const std::unordered_set<uint64_t>& hiddenShapeIds,
        bool generatedOnly) noexcept {

        float bestDistance = (std::numeric_limits<float>::max)();
        float bestVolume = (std::numeric_limits<float>::max)();
        uint64_t bestShapeId = 0u;
        for (const ASSETS::COLLISION::ModelCollisionShape& shape :
            setup.shapes) {

            if (hiddenShapeIds.contains(shape.id) ||
                (generatedOnly && !shape.generated)) {
                continue;
            }
            const Bounds bounds = ComputeModelCollisionShapeBounds(shape);
            float distance = 0.0f;
            if (!IntersectRayBounds(ray, bounds, distance)) {
                continue;
            }
            const float volume = BoundsVolume(bounds);
            const float distanceTolerance = (std::max)(
                0.001f,
                bestDistance * 0.002f);
            if (distance < bestDistance - distanceTolerance ||
                (std::abs(distance - bestDistance) <= distanceTolerance &&
                    volume < bestVolume)) {
                bestDistance = distance;
                bestVolume = volume;
                bestShapeId = shape.id;
            }
        }
        return bestShapeId;
    }

    int32_t PickModelCollisionSourceNode(
        const ModelCollisionPointerRay& ray,
        const std::vector<ModelCollisionPreviewNode>& nodes) noexcept {

        float bestDistance = (std::numeric_limits<float>::max)();
        float bestVolume = (std::numeric_limits<float>::max)();
        int32_t bestNodeIndex = -1;
        for (const ModelCollisionPreviewNode& node : nodes) {
            float distance = 0.0f;
            if (!IntersectRayBounds(ray, node.bounds, distance)) {
                continue;
            }
            const float volume = BoundsVolume(node.bounds);
            const float distanceTolerance = (std::max)(
                0.001f,
                bestDistance * 0.002f);
            if (distance < bestDistance - distanceTolerance ||
                (std::abs(distance - bestDistance) <= distanceTolerance &&
                    volume < bestVolume)) {
                bestDistance = distance;
                bestVolume = volume;
                bestNodeIndex = node.nodeIndex;
            }
        }
        return bestNodeIndex;
    }

} // namespace HIKARI::EDITOR
