#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"

#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    bool GpuDrivenLayer::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* staticRootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount) {

        (void)device;
        (void)staticRootSignature;
        (void)rootConstantParameterIndex;
        (void)rootConstantCount;
        return sceneBuffer_ != nullptr ||
            indirectDrawBuffer_ != nullptr ||
            clusterCullingPass_ != nullptr;
    }

    void GpuDrivenLayer::Attach(
        SurfaceGpuSceneFrameBuffer* sceneBuffer,
        SurfaceIndirectDrawBuffer* indirectDrawBuffer,
        CLUSTER::ClusterGpuCullingPass* clusterCullingPass) {

        sceneBuffer_ = sceneBuffer;
        indirectDrawBuffer_ = indirectDrawBuffer;
        clusterCullingPass_ = clusterCullingPass;
        frameContext_.scene.instanceBuffer = sceneBuffer_;
    }

    void GpuDrivenLayer::ResetFrame() {
        frameContext_ = {};
        frameContext_.scene.instanceBuffer = sceneBuffer_;
    }

    bool GpuDrivenLayer::BeginFrame(const GpuDrivenSceneSource* source) {
        ResetFrame();
        frameContext_.stats.sourceInstanceCount =
            source != nullptr
                ? source->CountGpuSceneInstances()
                : 0u;
        return true;
    }

    void GpuDrivenLayer::UploadSurfaceGpuSceneFrame(
        uint32_t instanceCount,
        uint32_t opaqueBaseIndex,
        uint32_t depthAwareBaseIndex,
        uint32_t transparentBaseIndex,
        bool sceneResident) {

        frameContext_.scene.instanceBuffer = sceneBuffer_;
        frameContext_.scene.instanceCount = instanceCount;
        frameContext_.scene.opaqueBaseIndex = opaqueBaseIndex;
        frameContext_.scene.depthAwareBaseIndex = depthAwareBaseIndex;
        frameContext_.scene.transparentBaseIndex = transparentBaseIndex;
        frameContext_.stats.residentInstanceCount = instanceCount;
        frameContext_.stats.sceneResident = sceneResident;
    }

    void GpuDrivenLayer::PrepareSurfaceGpuSceneMaterialFrame() {
    }

    void GpuDrivenLayer::DispatchVisibility(
        const CLUSTER::ClusterGpuCullingPassStats& stats) {

        if (clusterCullingPass_ == nullptr) {
            frameContext_.visibility = {};
            frameContext_.stats.visibilityReady = false;
            return;
        }

        frameContext_.visibility.visibleInstanceBuffer = nullptr;
        frameContext_.visibility.visibleClusterRangeBuffer =
            clusterCullingPass_->GetVisibleRangeBuffer();
        frameContext_.visibility.visibleMeshletRangeBuffer =
            clusterCullingPass_->GetVisibleRangeBuffer();
        frameContext_.visibility.counterBuffer =
            clusterCullingPass_->GetCounterBuffer();
        frameContext_.visibility.sourceSingleSidedInstanceCount =
            stats.sourceSingleSidedInstanceCount;
        frameContext_.visibility.sourceDoubleSidedInstanceCount =
            stats.sourceDoubleSidedInstanceCount;
        frameContext_.visibility.submittedDrawSeedCount =
            stats.submittedDrawSeedCount;
        frameContext_.stats.visibilitySeedCount =
            stats.submittedDrawSeedCount;
        frameContext_.stats.visibilityReady =
            stats.initialized &&
            stats.visibleRangeBufferReady &&
            stats.counterBufferReady;
    }

    void GpuDrivenLayer::BuildCommandBuffers() {
        frameContext_.commands = {};

        if (indirectDrawBuffer_ != nullptr) {
            frameContext_.commands.drawIndexedArgs =
                indirectDrawBuffer_->GetArgumentBuffer();
            frameContext_.commands.drawIndexedSignature =
                indirectDrawBuffer_->GetCommandSignature();
        }

        if (clusterCullingPass_ != nullptr) {
            frameContext_.commands.clusterDrawArgs =
                clusterCullingPass_->GetDrawArgumentBuffer();
            frameContext_.commands.meshletDispatchArgs =
                clusterCullingPass_->GetMeshletDispatchArgumentBuffer();
            frameContext_.commands.clusterDrawSignature =
                clusterCullingPass_->GetDrawCommandSignature();
            frameContext_.commands.meshletDispatchSignature =
                clusterCullingPass_->GetMeshletDispatchCommandSignature();

            GpuDrivenCommandLayout& layout = frameContext_.commands.layout;
            layout.drawArgumentBucketCapacity =
                clusterCullingPass_->GetDrawArgumentBucketCapacity();
            layout.backFaceDrawArgumentOffset =
                clusterCullingPass_->GetDrawArgumentBufferOffset(
                    CLUSTER::ClusterDrawCullModeBucket::BackFace);
            layout.doubleSidedDrawArgumentOffset =
                clusterCullingPass_->GetDrawArgumentBufferOffset(
                    CLUSTER::ClusterDrawCullModeBucket::DoubleSided);
            layout.backFaceCounterOffset =
                clusterCullingPass_->GetDrawCommandCounterOffset(
                    CLUSTER::ClusterDrawCullModeBucket::BackFace);
            layout.doubleSidedCounterOffset =
                clusterCullingPass_->GetDrawCommandCounterOffset(
                    CLUSTER::ClusterDrawCullModeBucket::DoubleSided);
            layout.meshletBackFaceDispatchOffset =
                clusterCullingPass_->GetMeshletDispatchArgumentBufferOffset(
                    CLUSTER::ClusterDrawCullModeBucket::BackFace);
            layout.meshletDoubleSidedDispatchOffset =
                clusterCullingPass_->GetMeshletDispatchArgumentBufferOffset(
                    CLUSTER::ClusterDrawCullModeBucket::DoubleSided);
        }

        frameContext_.stats.commandBuildReady =
            frameContext_.commands.clusterDrawArgs != nullptr ||
            frameContext_.commands.meshletDispatchArgs != nullptr ||
            frameContext_.commands.drawIndexedArgs != nullptr;
    }

    GeometryBackendContext GpuDrivenLayer::BuildGeometryBackendContext(
        ID3D12GraphicsCommandList* commandList,
        GpuDrivenPassKind pass,
        GeometryBackendKind backend) const {

        GeometryBackendContext context{};
        context.commandList = commandList;
        context.pass = pass;
        context.backend = backend;
        context.scene = &frameContext_.scene;
        context.visibility = &frameContext_.visibility;
        context.commands = &frameContext_.commands;
        return context;
    }

    SurfaceGpuSceneFrameBuffer* GpuDrivenLayer::GetSceneBuffer() const {
        return sceneBuffer_;
    }

    SurfaceIndirectDrawBuffer* GpuDrivenLayer::GetIndirectDrawBuffer() const {
        return indirectDrawBuffer_;
    }

    CLUSTER::ClusterGpuCullingPass* GpuDrivenLayer::GetClusterCullingPass() const {
        return clusterCullingPass_;
    }

    const GpuDrivenFrameContext& GpuDrivenLayer::GetFrameContext() const {
        return frameContext_;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
