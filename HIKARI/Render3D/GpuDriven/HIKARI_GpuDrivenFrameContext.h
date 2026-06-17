#pragma once

#include "Render3D/GpuDriven/HIKARI_GpuCommandBuildResult.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenStats.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuVisibilityResult.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenFrameContext {
        GpuSceneFrame scene{};
        GpuVisibilityResult visibility{};
        GpuCommandBuildResult commands{};
        GpuDrivenStats stats{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
