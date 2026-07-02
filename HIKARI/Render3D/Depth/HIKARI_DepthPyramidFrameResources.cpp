#include "Render3D/Depth/HIKARI_DepthPyramidFrameResources.h"

namespace HIKARI::RENDER3D::DEPTH {

    namespace {
        DepthPyramidFrameResources gFrameResources{};
    }

    void DepthPyramidFrameResources::BeginFrame(uint32_t nextFrameIndex) {
        frameIndex = nextFrameIndex;
        currentValid = false;
        current = {};
    }

    bool DepthPyramidFrameResources::PublishCurrent(const DepthPyramidView& view) {
        if (!view.valid || view.pyramidSrv.ptr == 0 || view.width == 0 || view.height == 0) {
            currentValid = false;
            current = {};
            return false;
        }

        frameIndex = view.frameIndex;
        currentValid = true;
        current = view;
        return true;
    }

    const DepthPyramidView* DepthPyramidFrameResources::TryGetCurrent() const {
        return currentValid && current.valid ? &current : nullptr;
    }

    const DepthPyramidView* DepthPyramidFrameResources::TryGetCurrent(
        DepthPyramidSourceKind requiredSource) const {

        const DepthPyramidView* view = TryGetCurrent();
        if (view == nullptr) {
            return nullptr;
        }
        return view->sourceKind == requiredSource ? view : nullptr;
    }

    void BeginDepthPyramidFrame(uint32_t frameIndex) {
        gFrameResources.BeginFrame(frameIndex);
    }

    bool PublishFrameDepthPyramid(const DepthPyramidView& view) {
        return gFrameResources.PublishCurrent(view);
    }

    const DepthPyramidFrameResources& GetDepthPyramidFrameResources() {
        return gFrameResources;
    }

    const DepthPyramidView* TryGetFrameDepthPyramidView() {
        return gFrameResources.TryGetCurrent();
    }

    const DepthPyramidView* TryGetFrameDepthPyramidView(DepthPyramidSourceKind requiredSource) {
        return gFrameResources.TryGetCurrent(requiredSource);
    }

    void ResetDepthPyramidFrameResources(uint32_t frameIndex) {
        BeginDepthPyramidFrame(frameIndex);
    }

    void PublishDepthPyramidView(const DepthPyramidView& view) {
        (void)PublishFrameDepthPyramid(view);
    }

} // namespace HIKARI::RENDER3D::DEPTH
