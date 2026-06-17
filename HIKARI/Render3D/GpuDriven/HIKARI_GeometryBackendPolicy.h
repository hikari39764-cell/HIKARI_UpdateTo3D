#pragma once

#include "Render3D/GpuDriven/HIKARI_GeometryBackendContext.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GeometryBackendPolicy {
        GeometryBackendKind preferred = GeometryBackendKind::CpuDirect;
        GeometryBackendKind fallback = GeometryBackendKind::CpuDirect;
        bool allowCpuDirectFallback = true;
        bool forcePreferredOnly = false;
    };

    GeometryBackendPolicy ResolveGeometryBackendPolicy(GpuDrivenPassKind pass);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
