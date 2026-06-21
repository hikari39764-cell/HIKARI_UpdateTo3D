#include "Render3D/GpuDriven/HIKARI_GeometryBackendPolicy.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {

        bool IsKnownGpuBackend(GeometryBackendKind backend) {
            switch (backend) {
            case GeometryBackendKind::GpuDrivenTraditionalVS:
            case GeometryBackendKind::GpuDrivenClusterVS:
            case GeometryBackendKind::GpuDrivenMeshShader:
                return true;
            default:
                return false;
            }
        }

    } // namespace

    bool GeometryBackendExecutionPlan::AddGpuBackend(
        GeometryBackendKind backend) {

        if (!IsKnownGpuBackend(backend)) {
            return false;
        }
        for (size_t i = 0; i < gpuBackendCount; ++i) {
            if (gpuBackends[i] == backend) {
                return true;
            }
        }
        if (gpuBackendCount >= gpuBackends.size()) {
            return false;
        }
        gpuBackends[gpuBackendCount++] = backend;
        return true;
    }

    GeometryBackendPolicy ResolveGeometryBackendPolicy(GpuDrivenPassKind pass) {
        GeometryBackendPolicy policy{};
        switch (pass) {
        case GpuDrivenPassKind::ForwardOpaque:
        case GpuDrivenPassKind::GeometryAux:
        case GpuDrivenPassKind::DepthPrepass:
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.secondary = GeometryBackendKind::GpuDrivenClusterVS;
            return policy;
        case GpuDrivenPassKind::Shadow:
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.secondary = GeometryBackendKind::GpuDrivenClusterVS;
            return policy;
        case GpuDrivenPassKind::ReflectionCapture:
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.secondary = GeometryBackendKind::GpuDrivenClusterVS;
            return policy;
        case GpuDrivenPassKind::DepthAware:
        case GpuDrivenPassKind::Transparent:
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.secondary = GeometryBackendKind::GpuDrivenClusterVS;
            return policy;
        case GpuDrivenPassKind::Debug:
        default:
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.secondary = GeometryBackendKind::GpuDrivenClusterVS;
            return policy;
        }
    }

    GeometryBackendExecutionPlan BuildGeometryBackendExecutionPlan(
        const GeometryBackendPolicy& policy) {

        GeometryBackendExecutionPlan plan{};
        plan.AddGpuBackend(policy.preferred);
        if (!policy.forcePreferredOnly) {
            plan.AddGpuBackend(policy.secondary);
        }
        return plan;
    }

    GeometryBackendExecutionPlan ResolveGeometryBackendExecutionPlan(
        GpuDrivenPassKind pass) {

        return BuildGeometryBackendExecutionPlan(
            ResolveGeometryBackendPolicy(pass));
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
