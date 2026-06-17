#pragma once

#include "Render3D/GpuDriven/HIKARI_GpuDrivenPassDesc.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenView.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    GpuDrivenView BuildGpuDrivenView(
        GpuDrivenPassKind pass,
        const MATH::Mat4& viewProj,
        const MATH::Vec3& viewPosition,
        uint32_t flags = 0);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
