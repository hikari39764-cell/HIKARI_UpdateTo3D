#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "Physics/HIKARI_IPhysicsWorldBackend.h"

namespace HIKARI {
    class World;
}

namespace HIKARI::PHYSICS {

    class PhysicsWorldService {
    public:
        ~PhysicsWorldService();

        PhysicsWorldService() = default;
        PhysicsWorldService(const PhysicsWorldService&) = delete;
        PhysicsWorldService& operator=(const PhysicsWorldService&) = delete;

        bool InstallBackend(
            std::unique_ptr<IPhysicsWorldBackend> backend);
        std::unique_ptr<IPhysicsWorldBackend> RemoveBackend();

        bool Configure(const PhysicsWorldSettings& settings);
        const PhysicsWorldSettings& GetSettings() const noexcept;
        bool AttachWorld(World& world);
        void DetachWorld() noexcept;

        bool HasBackend() const noexcept;
        bool IsWorldAttached() const noexcept;
        std::string_view GetBackendName() const noexcept;
        PhysicsBackendCapabilities GetCapabilities() const noexcept;

        PhysicsBodyHandle CreateBody(
            const PhysicsBodyCreateInfo& createInfo);
        bool DestroyBody(PhysicsBodyHandle body);
        bool SetBodyPose(
            PhysicsBodyHandle body,
            const PhysicsPose& pose,
            bool activate = true);
        bool SetKinematicTarget(
            PhysicsBodyHandle body,
            const PhysicsPose& pose);
        bool SetBodyVelocity(
            PhysicsBodyHandle body,
            const MATH::Vec3& linearVelocity,
            const MATH::Vec3& angularVelocity);
        bool TryGetBodyState(
            PhysicsBodyHandle body,
            PhysicsBodyState& outState) const;

        void Step(float fixedDeltaSeconds);
        std::vector<PhysicsContactEvent> ConsumeContactEvents();

        bool Raycast(
            const PhysicsRaycastQuery& query,
            PhysicsHit& outHit) const;
        bool ShapeCast(
            const PhysicsShapeCastQuery& query,
            PhysicsHit& outHit) const;
        std::vector<PhysicsHit> Overlap(
            const PhysicsOverlapQuery& query) const;

    private:
        std::unique_ptr<IPhysicsWorldBackend> backend_{};
        World* world_ = nullptr;
        PhysicsWorldSettings settings_{};
        std::vector<PhysicsContactEvent> contactEvents_{};
    };

} // namespace HIKARI::PHYSICS
