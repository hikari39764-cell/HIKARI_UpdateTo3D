#include "Render3D/Pipeline/HIKARI_RenderPipeline.h"

namespace HIKARI::RENDER3D {

    void RenderPipeline::Initialize() {
        initialized_ = true;
    }

    void RenderPipeline::Shutdown() {
        initialized_ = false;
    }

    void RenderPipeline::BeginFrame() {
        // Future owner of pass order, scene color/depth resources, and frame resources.
        (void)initialized_;
    }

    void RenderPipeline::RenderFrame() {
        // Phase 1 keeps the existing renderer entry points while the pipeline shell lands.
        (void)initialized_;
    }

    void RenderPipeline::EndFrame() {
        (void)initialized_;
    }

} // namespace HIKARI::RENDER3D
