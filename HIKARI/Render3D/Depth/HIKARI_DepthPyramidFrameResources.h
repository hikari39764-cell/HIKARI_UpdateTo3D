#pragma once

#include <cstdint>

#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"

namespace HIKARI::RENDER3D::DEPTH {

    struct DepthPyramidFrameResources {
        uint32_t frameIndex = 0;
        bool currentValid = false;
        DepthPyramidView current{};

        void BeginFrame(uint32_t nextFrameIndex = 0);
        bool PublishCurrent(const DepthPyramidView& view);
        const DepthPyramidView* TryGetCurrent() const;
        const DepthPyramidView* TryGetCurrent(DepthPyramidSourceKind requiredSource) const;
    };

    void BeginDepthPyramidFrame(uint32_t frameIndex = 0);
    bool PublishFrameDepthPyramid(const DepthPyramidView& view);
    const DepthPyramidFrameResources& GetDepthPyramidFrameResources();
    const DepthPyramidView* TryGetFrameDepthPyramidView();
    const DepthPyramidView* TryGetFrameDepthPyramidView(DepthPyramidSourceKind requiredSource);

    // Legacy wrappers while older passes are being retired.
    void ResetDepthPyramidFrameResources(uint32_t frameIndex = 0);
    void PublishDepthPyramidView(const DepthPyramidView& view);

} // namespace HIKARI::RENDER3D::DEPTH
