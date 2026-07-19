#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    struct GameplayCameraService;

    class CameraFollowSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "CameraFollowSystem"; }
        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;

    private:
        GameplayCameraService* cameraService_ = nullptr;
    };

} // namespace HIKARI
