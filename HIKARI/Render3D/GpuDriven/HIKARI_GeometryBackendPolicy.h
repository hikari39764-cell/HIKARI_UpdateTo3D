#pragma once

#include <array>
#include <cstddef>

#include "Render3D/GpuDriven/HIKARI_GeometryBackendContext.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    constexpr size_t kMaxGeometryBackendPlanBackends = 3;

    struct GeometryBackendPolicy {
        GeometryBackendKind preferred = GeometryBackendKind::GpuDrivenMeshShader;
        GeometryBackendKind secondary = GeometryBackendKind::GpuDrivenClusterVS;
        bool forcePreferredOnly = false;
    };

    struct GeometryBackendExecutionPlan {
        std::array<GeometryBackendKind, kMaxGeometryBackendPlanBackends> gpuBackends{};
        size_t gpuBackendCount = 0;

        bool AddGpuBackend(GeometryBackendKind backend);
    };

    GeometryBackendPolicy ResolveGeometryBackendPolicy(GpuDrivenPassKind pass);
    GeometryBackendExecutionPlan BuildGeometryBackendExecutionPlan(
        const GeometryBackendPolicy& policy);
    GeometryBackendExecutionPlan ResolveGeometryBackendExecutionPlan(
        GpuDrivenPassKind pass);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
