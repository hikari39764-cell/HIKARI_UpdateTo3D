#include "Render3D/Pipeline/HIKARI_RenderPipeline.h"

namespace HIKARI::RENDER3D {

    void RenderPipeline::Initialize() {
        initialized_ = true;
    }

    void RenderPipeline::Shutdown() {
        initialized_ = false;
    }

    void RenderPipeline::BeginFrame() {
        (void)initialized_;
    }

    void RenderPipeline::RenderFrame() {
        (void)initialized_;
    }
    
    void RenderPipeline::EndFrame() {
        (void)initialized_;
    }

} // namespace HIKARI::RENDER3D
