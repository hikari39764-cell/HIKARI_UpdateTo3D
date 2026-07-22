#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {
    struct GameplayCameraService;
    namespace GAMEPLAY { class MotionIntentService; }
    namespace PHYSICS {
        class KinematicMotionService;
        class PhysicsWorldService;
    }

    class CharacterLocomotionSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "CharacterLocomotionSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void FixedUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        GAMEPLAY::MotionIntentService* motionIntentService_ = nullptr;
        PHYSICS::KinematicMotionService* kinematicMotionService_ = nullptr;
        const PHYSICS::PhysicsWorldService* physicsService_ = nullptr;
        const GameplayCameraService* cameraService_ = nullptr;
    };

} // namespace HIKARI
