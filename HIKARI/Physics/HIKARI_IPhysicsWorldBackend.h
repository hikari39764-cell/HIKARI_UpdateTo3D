#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS {

    class IPhysicsWorldBackend {
    public:
        virtual ~IPhysicsWorldBackend() = default;

        virtual std::string_view GetBackendName() const noexcept = 0;
        virtual PhysicsBackendCapabilities GetCapabilities()
            const noexcept = 0;

        virtual bool Initialize(
            const PhysicsWorldSettings& settings) = 0;
        virtual void Shutdown() noexcept = 0;

        virtual PhysicsBodyHandle CreateBody(
            const PhysicsBodyCreateInfo& createInfo) = 0;
        virtual bool DestroyBody(PhysicsBodyHandle body) = 0;
        virtual bool SetBodyPose(
            PhysicsBodyHandle body,
            const PhysicsPose& pose,
            bool activate) = 0;
        virtual bool SetKinematicTarget(
            PhysicsBodyHandle body,
            const PhysicsPose& pose) = 0;
        virtual bool SetBodyVelocity(
            PhysicsBodyHandle body,
            const MATH::Vec3& linearVelocity,
            const MATH::Vec3& angularVelocity) = 0;
        virtual bool TryGetBodyState(
            PhysicsBodyHandle body,
            PhysicsBodyState& outState) const = 0;

        virtual void Step(float fixedDeltaSeconds) = 0;
        virtual void DrainContactEvents(
            std::vector<PhysicsContactEvent>& outEvents) = 0;

        virtual bool Raycast(
            const PhysicsRaycastQuery& query,
            PhysicsHit& outHit) const = 0;
        virtual bool ShapeCast(
            const PhysicsShapeCastQuery& query,
            PhysicsHit& outHit) const = 0;
        virtual void Overlap(
            const PhysicsOverlapQuery& query,
            std::vector<PhysicsHit>& outHits) const = 0;
    };

} // namespace HIKARI::PHYSICS
