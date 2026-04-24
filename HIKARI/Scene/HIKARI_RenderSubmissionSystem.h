#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Assets/HIKARI_Assets.h"

namespace HIKARI {

    struct SubmittedDrawItemDebugInfo {
        uint32_t gpuMeshId = 0;
        ASSET::AssetHandle<ASSET::MaterialAsset> material{};
        uint32_t hasBaseColorTexture = 0;
        uint32_t hasNormalTexture = 0;
        uint32_t hasOrmTexture = 0;
        uint32_t hasEmissiveTexture = 0;
        uint32_t postGroupMask = 0;
        ASSET::AssetState modelState = ASSET::AssetState::Unloaded;
    };

    struct RenderSubmissionDebugStats {
        int submittedModelCount = 0;
        int submittedDrawItemCount = 0;
        int fallbackWireCount = 0;
        std::vector<SubmittedDrawItemDebugInfo> items{};
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
