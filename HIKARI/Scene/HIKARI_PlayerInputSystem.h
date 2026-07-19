#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {
namespace INPUT { class InputService; }

class PlayerInputSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "PlayerInputSystem"; }
    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void PreUpdate(World& world, const FrameContext& frame) override;

private:
    INPUT::InputService* inputService_ = nullptr;
};

} // namespace HIKARI
