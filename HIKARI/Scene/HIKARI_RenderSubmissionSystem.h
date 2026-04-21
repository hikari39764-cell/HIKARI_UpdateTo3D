#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    struct RenderSubmissionDebugStats {
        int submittedModelCount = 0;
        int fallbackWireCount = 0;
    };

    class RenderSubmissionSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "RenderSubmissionSystem"; }

        void PreRender(World& world, const FrameContext& frame) override;

        static const RenderSubmissionDebugStats& GetDebugStats();

    private:
        static RenderSubmissionDebugStats sDebugStats_;
    };

} // namespace HIKARI