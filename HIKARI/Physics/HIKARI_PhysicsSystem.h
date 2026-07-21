#pragma once

#include <cstdint>
#include <unordered_map>

#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {
    struct RuntimePlayStateService;
}

namespace HIKARI::PHYSICS {

    class PhysicsWorldService;
    class PhysicsCollisionGeometryStore;

    class PhysicsSystem final : public ISystem {
    public:
        explicit PhysicsSystem(
            PhysicsWorldSettings settings = {});

        std::string_view GetName() const override {
            return "PhysicsSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PreFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void FixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void PostFixedUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        struct BodyBinding {
            RuntimeObjectHandle object{};
            PhysicsBodyHandle body{};
            PhysicsMotionType motionType = PhysicsMotionType::Static;
            uint64_t definitionSignature = 0;
            PhysicsPose lastPushedPose{};
            bool hasLastPushedPose = false;
        };

        bool ShouldSimulate() const noexcept;
        void ReconcileBodies(World& world);
        void DestroyBindings() noexcept;
        void PushSceneDrivenPoses(World& world);
        void PullDynamicPoses(World& world);

        PhysicsWorldSettings settings_{};
        PhysicsWorldService* service_ = nullptr;
        PhysicsCollisionGeometryStore* collisionGeometryStore_ = nullptr;
        const RuntimePlayStateService* runtimePlayState_ = nullptr;
        std::unordered_map<uint64_t, BodyBinding> bindings_{};
    };

} // namespace HIKARI::PHYSICS
