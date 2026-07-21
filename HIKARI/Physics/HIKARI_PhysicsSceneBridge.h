#pragma once

#include <cstdint>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI {
    class GameObject;
}

namespace HIKARI::PHYSICS {

    class PhysicsCollisionGeometryStore;

    bool BuildPhysicsBodyCreateInfo(
        const GameObject& object,
        PhysicsBodyCreateInfo& outCreateInfo,
        MATH::Vec3& outWorldScale,
        PhysicsCollisionGeometryStore* collisionGeometryStore = nullptr);

    uint64_t ComputePhysicsBodyDefinitionSignature(
        const PhysicsBodyCreateInfo& createInfo,
        const MATH::Vec3& worldScale) noexcept;

    bool TryGetPhysicsWorldPoseAndScale(
        const GameObject& object,
        PhysicsPose& outPose,
        MATH::Vec3& outScale);

    bool ArePhysicsPosesNearlyEqual(
        const PhysicsPose& left,
        const PhysicsPose& right) noexcept;

    bool ApplyPhysicsWorldPose(
        GameObject& object,
        const PhysicsPose& pose);

} // namespace HIKARI::PHYSICS
