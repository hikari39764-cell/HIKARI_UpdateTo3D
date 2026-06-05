#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class Camera3D;

    class CameraFollowSystem final : public ISystem {
    public:
        explicit CameraFollowSystem(Camera3D& camera);

        std::string_view GetName() const override { return "CameraFollowSystem"; }
        void Update(World& world, const FrameContext& frame) override;

    private:
        Camera3D* camera_ = nullptr;
    };

} // namespace HIKARI
