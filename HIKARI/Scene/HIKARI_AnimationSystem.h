#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class AnimationSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "AnimationSystem"; }
        void Update(World& world, const FrameContext& frame) override;
    };

} // namespace HIKARI
