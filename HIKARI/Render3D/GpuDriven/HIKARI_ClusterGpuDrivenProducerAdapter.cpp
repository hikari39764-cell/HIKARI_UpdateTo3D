#include "Render3D/GpuDriven/HIKARI_ClusterGpuDrivenProducerAdapter.h"

#include <array>

#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        constexpr GpuDrivenCommandBucket ToGpuDrivenCommandBucket(
            CLUSTER::ClusterDrawCullModeBucket bucket) {

            return bucket == CLUSTER::ClusterDrawCullModeBucket::DoubleSided
                ? GpuDrivenCommandBucket::DoubleSided
                : GpuDrivenCommandBucket::BackFaceCulled;
        }

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

        void FillClusterPassOutput(
            GpuVisibilityResult& visibility,
            GpuCommandBuildResult& commands,
            GpuDrivenPassKind pass,
            const CLUSTER::ClusterGpuCullingPassStats& stats,
            const CLUSTER::ClusterGpuCullingPass& cullingPass) {

            GpuVisibilityPassResult& passVisibility =
                visibility.GetPass(pass);
            passVisibility.buckets[ToCommandBucketIndex(
                GpuDrivenCommandBucket::BackFaceCulled)].sourceInstanceCount =
                stats.sourceSingleSidedInstanceCount;
            passVisibility.buckets[ToCommandBucketIndex(
                GpuDrivenCommandBucket::DoubleSided)].sourceInstanceCount =
                stats.sourceDoubleSidedInstanceCount;
            passVisibility.submittedDrawSeedCount =
                stats.submittedDrawSeedCount;

            GpuDrivenCommandPassLayout& layout =
                commands.layout.GetPass(pass);
            layout.commandBucketCapacity =
                cullingPass.GetDrawArgumentBucketCapacity();
            constexpr CLUSTER::ClusterDrawCullModeBucket sourceBuckets[] = {
                CLUSTER::ClusterDrawCullModeBucket::BackFace,
                CLUSTER::ClusterDrawCullModeBucket::DoubleSided,
            };
            for (const CLUSTER::ClusterDrawCullModeBucket sourceBucket :
                sourceBuckets) {

                GpuDrivenCommandBucketLayout& bucketLayout =
                    layout.GetBucket(ToGpuDrivenCommandBucket(sourceBucket));
                bucketLayout.gpuDrawIndexedArgumentOffset =
                    cullingPass.GetDrawArgumentBufferOffset(sourceBucket);
                bucketLayout.meshDispatchArgumentOffset =
                    cullingPass.GetMeshletDispatchArgumentBufferOffset(sourceBucket);
                bucketLayout.counterOffset =
                    cullingPass.GetDrawCommandCounterOffset(sourceBucket);
            }
        }
    }

    void ClusterGpuDrivenProducerAdapter::Attach(
        CLUSTER::ClusterGpuCullingPass* cullingPass) {

        cullingPass_ = cullingPass;
    }

    bool ClusterGpuDrivenProducerAdapter::IsAttached() const {
        return cullingPass_ != nullptr;
    }

    GpuDrivenProducerKind ClusterGpuDrivenProducerAdapter::GetProducerKind() const {
        return GpuDrivenProducerKind::ClusterGpuCulling;
    }

    void ClusterGpuDrivenProducerAdapter::BeginFrame(bool collectCounterReadback) {
        if (cullingPass_ != nullptr) {
            cullingPass_->BeginFrame(collectCounterReadback);
        }
    }

    GpuDrivenProducerFrameOutput ClusterGpuDrivenProducerAdapter::BuildFrameOutput() const {
        GpuDrivenProducerFrameOutput output{};
        output.producerKind = GetProducerKind();
        if (cullingPass_ == nullptr) {
            return output;
        }

        const CLUSTER::ClusterGpuCullingPassStats& stats = cullingPass_->GetStats();
        output.visibility.visibleInstanceBuffer = nullptr;
        output.visibility.visibleClusterRangeBuffer =
            cullingPass_->GetVisibleRangeBuffer();
        output.visibility.visibleMeshletRangeBuffer =
            cullingPass_->GetVisibleRangeBuffer();
        output.visibility.counterBuffer =
            cullingPass_->GetCounterBuffer();
        output.visibilitySeedCount =
            stats.submittedDrawSeedCount;
        output.visibilityReady =
            stats.initialized &&
            stats.visibleRangeBufferReady &&
            stats.counterBufferReady;

        output.commands.gpuDrawIndexedArgs =
            cullingPass_->GetDrawArgumentBuffer();
        output.commands.meshDispatchArgs =
            cullingPass_->GetMeshletDispatchArgumentBuffer();
        output.commands.gpuDrawIndexedSignature =
            cullingPass_->GetDrawCommandSignature();
        output.commands.meshDispatchSignature =
            cullingPass_->GetMeshletDispatchCommandSignature();

        FillClusterPassOutput(
            output.visibility,
            output.commands,
            GpuDrivenPassKind::ForwardOpaque,
            stats,
            *cullingPass_);
        FillClusterPassOutput(
            output.visibility,
            output.commands,
            GpuDrivenPassKind::GeometryAux,
            stats,
            *cullingPass_);

        output.commandBuildReady =
            output.commands.gpuDrawIndexedArgs != nullptr ||
            output.commands.meshDispatchArgs != nullptr;
        return output;
    }

    GpuDrivenProducerWorkResult ClusterGpuDrivenProducerAdapter::DispatchWork(
        const GpuDrivenProducerWorkContext& context) {

        GpuDrivenProducerWorkResult result{};
        if (context.frame != nullptr) {
            result.sourcePassCount =
                context.frame->CountClusterEligiblePasses();
            result.sourceInstanceCount =
                context.frame->CountClusterEligibleInstances();
        }

        if (cullingPass_ == nullptr) {
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

        result.submitted = cullingPass_->Dispatch(
            context.commandList,
            context.viewProj,
            context.cameraPosition,
            context.geometryPoolSrv,
            context.surfaceGpuSceneGpuAddress,
            rangeCount != 0 ? ranges.data() : nullptr,
            rangeCount);
        return result;
    }

    const CLUSTER::ClusterGpuCullingPassStats*
        ClusterGpuDrivenProducerAdapter::GetClusterStats() const {

        return cullingPass_ != nullptr ? &cullingPass_->GetStats() : nullptr;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
