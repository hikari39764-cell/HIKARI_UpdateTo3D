#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"

#include <algorithm>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        GpuDrivenPassKind PassFromIndex(size_t index) {
            return static_cast<GpuDrivenPassKind>(
                (std::min)(index, kGpuDrivenPassCount - 1u));
        }

        GpuDrivenPassKind ResolveExecutionSourcePass(
            GpuDrivenPassKind pass) {

            return pass == GpuDrivenPassKind::GeometryAux
                ? GpuDrivenPassKind::ForwardOpaque
                : pass;
        }

        bool UsesGeometryAuxPipeline(GpuDrivenPassKind pass) {
            return pass == GpuDrivenPassKind::GeometryAux;
        }

        bool HasCommandBucketCapacity(
            const GpuCommandBuildResult& commands,
            GpuDrivenPassKind pass) {

            return commands.layout.GetPass(pass).commandBucketCapacity != 0;
        }
    }

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
        InitializePassExecutionStates(nullptr);
    }

    bool GpuDrivenLayer::BeginFrame(const GpuDrivenSceneSource* source) {
        ResetFrame();
        frameContext_.stats.sourceInstanceCount =
            source != nullptr
                ? source->CountGpuSceneInstances()
                : 0u;
        InitializePassExecutionStates(source);
        RefreshPassExecutionStates();
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
        frameContext_.stats.visibilityOverflowInstanceCount =
            output.visibilityOverflowInstanceCount;
        frameContext_.stats.commandBuildReady =
            output.commandBuildReady ||
            frameContext_.commands.surfaceDrawIndexedArgs != nullptr;
        RefreshPassExecutionStates();
    }

    void GpuDrivenLayer::SetBackendAvailability(
        const GpuDrivenBackendAvailability& availability) {

        frameContext_.backendAvailability = availability;
        RefreshPassExecutionStates();
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
        RefreshPassExecutionStates();
    }

    bool GpuDrivenLayer::IsBackendConsumable(
        GpuDrivenPassKind pass,
        GeometryBackendKind backend) const {

        const GpuDrivenPassExecutionState& state =
            GetPassExecutionState(pass);
        switch (backend) {
        case GeometryBackendKind::GpuDrivenMeshShader:
            return state.meshShaderConsumable;
        case GeometryBackendKind::GpuDrivenClusterVS:
            return state.clusterVsConsumable;
        case GeometryBackendKind::GpuDrivenTraditionalVS:
            return state.traditionalIndirectConsumable;
        case GeometryBackendKind::CpuDirect:
        default:
            return false;
        }
    }

    bool GpuDrivenLayer::IsPassGpuReady(GpuDrivenPassKind pass) const {
        return GetPassExecutionState(pass).gpuBackendReady;
    }

    GeometryBackendExecutionPlan GpuDrivenLayer::GetPassExecutionPlan(
        GpuDrivenPassKind pass) const {

        const GeometryBackendPolicy policy =
            ResolveGeometryBackendPolicy(pass);
        GeometryBackendExecutionPlan plan{};
        plan.runCpuDirectTail =
            policy.allowCpuDirectFallback &&
            !policy.forcePreferredOnly;

        if (IsBackendConsumable(pass, policy.preferred)) {
            plan.AddGpuBackend(policy.preferred);
        }
        if (!policy.forcePreferredOnly &&
            IsBackendConsumable(pass, policy.fallback)) {
            plan.AddGpuBackend(policy.fallback);
        }
        return plan;
    }

    GeometryBackendContext GpuDrivenLayer::BuildGeometryBackendContext(
        ID3D12GraphicsCommandList* commandList,
        GpuDrivenPassKind pass,
        GeometryBackendKind backend) const {

        GeometryBackendContext context{};
        context.commandList = commandList;
        context.requestedPass = pass;
        context.pass = GetPassExecutionState(pass).visibilityPass;
        context.backend = backend;
        context.scene = &frameContext_.scene;
        context.visibility = &frameContext_.visibility;
        context.commands = &frameContext_.commands;
        return context;
    }

    const GpuDrivenPassExecutionState&
        GpuDrivenLayer::GetPassExecutionState(
            GpuDrivenPassKind pass) const {

        return frameContext_.passExecution[ToPassIndex(pass)];
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

    void GpuDrivenLayer::InitializePassExecutionStates(
        const GpuDrivenSceneSource* source) {

        for (size_t passIndex = 0; passIndex < kGpuDrivenPassCount; ++passIndex) {
            GpuDrivenPassExecutionState& state =
                frameContext_.passExecution[passIndex];
            state = {};
            state.pass = PassFromIndex(passIndex);
            state.sourcePass = ResolveExecutionSourcePass(state.pass);
            state.visibilityPass = state.sourcePass;
            state.sourceMode =
                state.sourcePass == state.pass
                    ? GpuDrivenPassSourceMode::Own
                    : GpuDrivenPassSourceMode::Derived;
            state.cpuFallbackAllowed =
                ResolveGeometryBackendPolicy(state.pass).allowCpuDirectFallback;

            if (source == nullptr) {
                state.sourceMode = GpuDrivenPassSourceMode::None;
                continue;
            }

            const GpuDrivenPassSource& passSource =
                source->GetPass(state.sourcePass);
            state.hasSource = passSource.HasGpuSceneRange();
            state.clusterEligible = passSource.clusterEligible;
            state.sourceInstanceCount = passSource.gpuSceneInstanceCount;
            if (!state.hasSource) {
                state.sourceMode = GpuDrivenPassSourceMode::None;
            }
        }
    }

    void GpuDrivenLayer::RefreshPassExecutionStates() {
        for (GpuDrivenPassExecutionState& state :
            frameContext_.passExecution) {

            const GpuVisibilityPassResult& visibility =
                frameContext_.visibility.GetPass(state.visibilityPass);
            const GpuDrivenPassKind commandPass = state.visibilityPass;
            const bool hasBucketCapacity =
                HasCommandBucketCapacity(frameContext_.commands, commandPass);

            state.drawSeedCount = visibility.submittedDrawSeedCount;
            state.hasDrawSeeds = state.drawSeedCount != 0;
            state.overflowBlocked =
                frameContext_.stats.visibilityOverflowInstanceCount != 0;
            state.visibilityReady =
                frameContext_.stats.visibilityReady &&
                state.hasDrawSeeds &&
                frameContext_.visibility.counterBuffer != nullptr &&
                !state.overflowBlocked;
            state.commandBuildReady =
                frameContext_.stats.commandBuildReady &&
                hasBucketCapacity;

            const bool useGeometryPipeline =
                UsesGeometryAuxPipeline(state.pass);
            const bool meshPipelineReady =
                useGeometryPipeline
                    ? frameContext_.backendAvailability.meshShaderGeometryAuxPipelineReady
                    : frameContext_.backendAvailability.meshShaderForwardPipelineReady;
            const bool clusterPipelineReady =
                useGeometryPipeline
                    ? frameContext_.backendAvailability.clusterVsGeometryAuxPipelineReady
                    : frameContext_.backendAvailability.clusterVsForwardPipelineReady;

            state.meshShaderConsumable =
                state.hasSource &&
                state.clusterEligible &&
                state.visibilityReady &&
                state.commandBuildReady &&
                frameContext_.commands.meshDispatchArgs != nullptr &&
                frameContext_.commands.meshDispatchSignature != nullptr &&
                meshPipelineReady;
            state.clusterVsConsumable =
                state.hasSource &&
                state.clusterEligible &&
                state.visibilityReady &&
                state.commandBuildReady &&
                frameContext_.commands.gpuDrawIndexedArgs != nullptr &&
                frameContext_.commands.gpuDrawIndexedSignature != nullptr &&
                clusterPipelineReady;
            state.traditionalIndirectConsumable =
                state.hasSource &&
                frameContext_.commands.surfaceDrawIndexedArgs != nullptr &&
                frameContext_.commands.surfaceDrawIndexedSignature != nullptr &&
                frameContext_.backendAvailability.traditionalIndirectPipelineReady;
            state.gpuBackendReady =
                state.meshShaderConsumable ||
                state.clusterVsConsumable ||
                state.traditionalIndirectConsumable;
        }
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
