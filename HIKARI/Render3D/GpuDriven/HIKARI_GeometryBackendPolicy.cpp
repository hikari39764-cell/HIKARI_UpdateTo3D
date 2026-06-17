#include "Render3D/GpuDriven/HIKARI_GeometryBackendPolicy.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

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
        case GpuDrivenPassKind::Debug:
        default:
            policy.preferred = GeometryBackendKind::CpuDirect;
            policy.fallback = GeometryBackendKind::CpuDirect;
            policy.allowCpuDirectFallback = true;
            return policy;
        }
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
