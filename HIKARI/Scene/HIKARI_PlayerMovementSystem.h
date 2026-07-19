#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    struct GameplayCameraService;

    class PlayerMovementSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "PlayerMovementSystem"; }
        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void FixedUpdate(World& world, const FrameContext& frame) override;

    private:
        const GameplayCameraService* cameraService_ = nullptr;
    };

} // namespace HIKARI
