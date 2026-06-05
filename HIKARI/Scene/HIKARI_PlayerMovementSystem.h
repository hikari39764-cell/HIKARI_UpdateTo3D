#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class Camera3D;

    class PlayerMovementSystem final : public ISystem {
    public:
        explicit PlayerMovementSystem(const Camera3D* camera = nullptr);

        std::string_view GetName() const override { return "PlayerMovementSystem"; }
        void Update(World& world, const FrameContext& frame) override;

    private:
        const Camera3D* camera_ = nullptr;
    };

} // namespace HIKARI
