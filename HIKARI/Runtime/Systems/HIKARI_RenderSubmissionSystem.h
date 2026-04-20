#pragma once

#include "Runtime/Systems/HIKARI_ISystem.h"

namespace HIKARI {

class RenderSubmissionSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "RenderSubmissionSystem"; }
    void Render(World& world, const FrameContext& frame) override;
};

} // namespace HIKARI
