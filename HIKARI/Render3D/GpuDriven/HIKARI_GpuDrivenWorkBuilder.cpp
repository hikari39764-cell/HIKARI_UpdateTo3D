#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"

#include <array>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    CLUSTER::ClusterGpuCullingPassKind ToClusterCullPassKind(
        GpuDrivenPassKind passKind) {

        switch (passKind) {
        case GpuDrivenPassKind::ForwardDepthAware:
            return CLUSTER::ClusterGpuCullingPassKind::ForwardDepthAware;
        case GpuDrivenPassKind::ForwardTransparent:
            return CLUSTER::ClusterGpuCullingPassKind::ForwardTransparent;
        case GpuDrivenPassKind::Shadow:
            return CLUSTER::ClusterGpuCullingPassKind::Shadow;
        case GpuDrivenPassKind::ForwardOpaque:
        default:
            return CLUSTER::ClusterGpuCullingPassKind::ForwardOpaque;
        }
    }

    GpuDrivenClusterWorkResult BuildGpuDrivenClusterWork(
        const GpuDrivenClusterWorkContext& context) {

        GpuDrivenClusterWorkResult result{};
        if (context.frame != nullptr) {
            result.sourcePassCount = context.frame->CountClusterEligiblePasses();
            result.sourceInstanceCount =
                context.frame->CountClusterEligibleInstances();
        }

        if (context.cullingPass == nullptr) {
            return result;
        }

        std::array<CLUSTER::ClusterGpuCullingSourceRange, 1> ranges{};
        size_t rangeCount = 0;

        if (context.frame != nullptr) {
            const GpuDrivenPassFrame& forwardOpaque =
                context.frame->GetPass(GpuDrivenPassKind::ForwardOpaque);
            if (forwardOpaque.HasSource() && forwardOpaque.clusterEligible) {
                const GpuSceneRange& range = forwardOpaque.gpuSceneRange;
                ranges[0] = {
                    range.baseIndex,
                    range.instanceCount,
                    ToClusterCullPassKind(range.passKind),
                    0u,
                    0u
                };
                rangeCount = 1;
            }
        }

        result.submitted = context.cullingPass->Dispatch(
            context.commandList,
            context.viewProj,
            context.cameraPosition,
            context.clusterGeometryPoolSrv,
            context.surfaceGpuSceneGpuAddress,
            rangeCount != 0 ? ranges.data() : nullptr,
            rangeCount);
        result.stats = &context.cullingPass->GetStats();
        return result;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
