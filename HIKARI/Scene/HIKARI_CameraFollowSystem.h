#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class Camera3D;

    class CameraFollowSystem final : public ISystem {
    public:
        CameraFollowSystem(Camera3D& camera, const bool& runtimeCameraActive);

        std::string_view GetName() const override { return "CameraFollowSystem"; }
        void Update(World& world, const FrameContext& frame) override;

    private:
        Camera3D* camera_ = nullptr;
        const bool* runtimeCameraActive_ = nullptr;
    };

} // namespace HIKARI
