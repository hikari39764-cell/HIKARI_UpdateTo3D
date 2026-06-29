#pragma once

#include <cstdint>

#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"

namespace HIKARI::RENDER3D::DEPTH {

    struct DepthPyramidFrameResources {
        uint32_t frameIndex = 0;
        bool currentValid = false;
        DepthPyramidView current{};

        void Reset(uint32_t nextFrameIndex = 0);
        const DepthPyramidView* TryGetCurrent() const;
    };

    void ResetDepthPyramidFrameResources(uint32_t frameIndex = 0);
    void PublishDepthPyramidView(const DepthPyramidView& view);
    const DepthPyramidFrameResources& GetDepthPyramidFrameResources();
    const DepthPyramidView* TryGetFrameDepthPyramidView();

} // namespace HIKARI::RENDER3D::DEPTH
