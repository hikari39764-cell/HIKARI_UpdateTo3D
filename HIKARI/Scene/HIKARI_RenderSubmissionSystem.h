#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

class RenderSubmissionSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "RenderSubmissionSystem"; }

    void PreRender(World& world, const FrameContext& frame) override;
};

} // namespace HIKARI
