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
            producer_ != nullptr;
    }

    void GpuDrivenLayer::Attach(
        SurfaceGpuSceneFrameBuffer* sceneBuffer,
        SurfaceIndirectDrawBuffer* indirectDrawBuffer,
        IGpuDrivenProducer* producer) {

        sceneBuffer_ = sceneBuffer;
        indirectDrawBuffer_ = indirectDrawBuffer;
        producer_ = producer;
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

    void GpuDrivenLayer::ImportProducerOutput(
        const GpuDrivenProducerFrameOutput& output) {

        ID3D12Resource* surfaceDrawIndexedArgs =
            frameContext_.commands.surfaceDrawIndexedArgs;
        ID3D12CommandSignature* surfaceDrawIndexedSignature =
            frameContext_.commands.surfaceDrawIndexedSignature;

        frameContext_.visibility = output.visibility;
        frameContext_.commands = output.commands;
        frameContext_.commands.surfaceDrawIndexedArgs =
            surfaceDrawIndexedArgs;
        frameContext_.commands.surfaceDrawIndexedSignature =
            surfaceDrawIndexedSignature;
        frameContext_.stats.visibilityReady = output.visibilityReady;
        frameContext_.stats.visibilitySeedCount = output.visibilitySeedCount;
        frameContext_.stats.commandBuildReady =
            output.commandBuildReady ||
            frameContext_.commands.surfaceDrawIndexedArgs != nullptr;
    }

    void GpuDrivenLayer::BuildCommandBuffers() {
        if (indirectDrawBuffer_ != nullptr) {
            frameContext_.commands.surfaceDrawIndexedArgs =
                indirectDrawBuffer_->GetArgumentBuffer();
            frameContext_.commands.surfaceDrawIndexedSignature =
                indirectDrawBuffer_->GetCommandSignature();
        } else {
            frameContext_.commands.surfaceDrawIndexedArgs = nullptr;
            frameContext_.commands.surfaceDrawIndexedSignature = nullptr;
        }

        frameContext_.stats.commandBuildReady =
            frameContext_.commands.gpuDrawIndexedArgs != nullptr ||
            frameContext_.commands.meshDispatchArgs != nullptr ||
            frameContext_.commands.surfaceDrawIndexedArgs != nullptr;
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

    IGpuDrivenProducer* GpuDrivenLayer::GetProducer() const {
        return producer_;
    }

    const GpuDrivenFrameContext& GpuDrivenLayer::GetFrameContext() const {
        return frameContext_;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
