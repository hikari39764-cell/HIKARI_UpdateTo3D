#include "Render3D/GpuDriven/HIKARI_GeometryBackendPolicy.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {

        bool IsGpuBackend(GeometryBackendKind backend) {
            return backend != GeometryBackendKind::CpuDirect;
        }

    } // namespace

    bool GeometryBackendExecutionPlan::AddGpuBackend(
        GeometryBackendKind backend) {

        if (!IsGpuBackend(backend)) {
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
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.fallback = GeometryBackendKind::GpuDrivenClusterVS;
            policy.allowCpuDirectFallback = true;
            return policy;
        case GpuDrivenPassKind::Shadow:
            policy.preferred = GeometryBackendKind::GpuDrivenTraditionalVS;
            policy.fallback = GeometryBackendKind::CpuDirect;
            policy.allowCpuDirectFallback = true;
            return policy;
        case GpuDrivenPassKind::ReflectionCapture:
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.fallback = GeometryBackendKind::GpuDrivenClusterVS;
            policy.allowCpuDirectFallback = true;
            return policy;
        case GpuDrivenPassKind::DepthAware:
        case GpuDrivenPassKind::Transparent:
            policy.preferred = GeometryBackendKind::GpuDrivenTraditionalVS;
            policy.fallback = GeometryBackendKind::CpuDirect;
            policy.allowCpuDirectFallback = true;
            return policy;
        case GpuDrivenPassKind::Debug:
        default:
            policy.preferred = GeometryBackendKind::CpuDirect;
            policy.fallback = GeometryBackendKind::CpuDirect;
            policy.allowCpuDirectFallback = true;
            return policy;
        }
    }

    GeometryBackendExecutionPlan BuildGeometryBackendExecutionPlan(
        const GeometryBackendPolicy& policy) {

        GeometryBackendExecutionPlan plan{};
        plan.runCpuDirectTail =
            policy.allowCpuDirectFallback &&
            !policy.forcePreferredOnly;

        plan.AddGpuBackend(policy.preferred);
        if (!policy.forcePreferredOnly) {
            plan.AddGpuBackend(policy.fallback);
        }
        return plan;
    }

    GeometryBackendExecutionPlan ResolveGeometryBackendExecutionPlan(
        GpuDrivenPassKind pass) {

        return BuildGeometryBackendExecutionPlan(
            ResolveGeometryBackendPolicy(pass));
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
