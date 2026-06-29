#include "Render3D/Depth/HIKARI_DepthPyramidFrameResources.h"

namespace HIKARI::RENDER3D::DEPTH {

    namespace {
        DepthPyramidFrameResources gFrameResources{};
    }

    void DepthPyramidFrameResources::Reset(uint32_t nextFrameIndex) {
        frameIndex = nextFrameIndex;
        currentValid = false;
        current = {};
    }

    const DepthPyramidView* DepthPyramidFrameResources::TryGetCurrent() const {
        return currentValid && current.valid ? &current : nullptr;
    }

    void ResetDepthPyramidFrameResources(uint32_t frameIndex) {
        gFrameResources.Reset(frameIndex);
    }

    void PublishDepthPyramidView(const DepthPyramidView& view) {
        if (!view.valid || view.pyramidSrv.ptr == 0 || view.width == 0 || view.height == 0) {
            gFrameResources.currentValid = false;
            gFrameResources.current = {};
            return;
        }

        gFrameResources.frameIndex = view.frameIndex;
        gFrameResources.currentValid = true;
        gFrameResources.current = view;
    }

    const DepthPyramidFrameResources& GetDepthPyramidFrameResources() {
        return gFrameResources;
    }

    const DepthPyramidView* TryGetFrameDepthPyramidView() {
        return gFrameResources.TryGetCurrent();
    }

} // namespace HIKARI::RENDER3D::DEPTH
