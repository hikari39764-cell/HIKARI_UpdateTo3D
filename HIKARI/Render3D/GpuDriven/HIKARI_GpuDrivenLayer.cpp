#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"

#include <algorithm>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(
                    value,
                    static_cast<size_t>(UINT32_MAX)));
        }

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

        size_t CountViewInstances(
            const GpuDrivenTraditionalIndirectView& view) {

            return view.gpuSceneInstanceCount != 0u
                ? view.gpuSceneInstanceCount
                : (view.instances != nullptr ? view.instances->size() : 0u);
        }

        size_t CountPrimaryInstances(const GpuDrivenPassSource& pass) {
            return pass.gpuSceneInstanceCount != 0u
                ? pass.gpuSceneInstanceCount
                : (pass.instances != nullptr ? pass.instances->size() : 0u);
        }

        size_t CountPassInstances(const GpuDrivenPassSource& pass) {
            return CountPrimaryInstances(pass) +
                CountViewInstances(pass.traditionalIndirect);
        }

        uint32_t ResolvePassBaseIndex(const GpuDrivenPassSource& pass) {
            return pass.HasPrimaryGpuSceneRange()
                ? pass.gpuSceneBaseIndex
                : pass.traditionalIndirect.gpuSceneBaseIndex;
        }

        void UploadSceneInstances(
            SurfaceGpuSceneFrameBuffer& buffer,
            const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
            size_t count) {

            if (count == 0u) {
                return;
            }
            if (instances != nullptr && !instances->empty()) {
                buffer.Upload(instances->data(), instances->size());
                return;
            }
            buffer.Upload(nullptr, count);
        }

        void UploadPassInstances(
            SurfaceGpuSceneFrameBuffer& buffer,
            const GpuDrivenPassSource& pass) {

            UploadSceneInstances(
                buffer,
                pass.instances,
                CountPrimaryInstances(pass));
            UploadSceneInstances(
                buffer,
                pass.traditionalIndirect.instances,
                CountViewInstances(pass.traditionalIndirect));
        }

        bool PatchDirtyPass(
            SurfaceGpuSceneFrameBuffer& buffer,
            const GpuDrivenPassSource& pass) {

            if (!pass.HasDirtyGpuSceneRanges()) {
                return true;
            }
            if (pass.instances == nullptr) {
                return false;
            }
            for (const GpuSceneDirtyRange& range : pass.dirtyRanges) {
                if (!range.IsValid()) {
                    continue;
                }
                const size_t localBegin = range.firstInstance;
                const size_t localCount = range.instanceCount;
                if (localBegin >= pass.instances->size() ||
                    localCount > pass.instances->size() - localBegin) {
                    return false;
                }
                if (!buffer.UpdateRange(
                    static_cast<size_t>(pass.gpuSceneBaseIndex) + localBegin,
                    pass.instances->data() + localBegin,
                    localCount)) {
                    return false;
                }
            }
            return true;
        }

        bool PatchDirtySceneRanges(
            SurfaceGpuSceneFrameBuffer& buffer,
            const GpuDrivenSceneSource& source) {

            for (size_t passIndex = 0u;
                passIndex < kGpuDrivenPassCount;
                ++passIndex) {

                if (!PatchDirtyPass(buffer, source.passes[passIndex])) {
                    return false;
                }
            }
            return true;
        }

        void UploadFullScene(
            SurfaceGpuSceneFrameBuffer& buffer,
            const GpuDrivenSceneSource& source) {

            buffer.ResetFrame();
            for (size_t passIndex = 0u;
                passIndex < kGpuDrivenPassCount;
                ++passIndex) {

                UploadPassInstances(buffer, source.passes[passIndex]);
            }
        }
    }

    void GpuDrivenSceneResidency::Reset() {
        resident = false;
        layoutVersion = 0;
        sourceVersion = 0;
        instanceCount = 0;
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
        frameSource_ = nullptr;
        sceneUploadStats_ = {};
        InitializePassExecutionStates(nullptr);
    }

    bool GpuDrivenLayer::BeginFrame(const GpuDrivenSceneSource* source) {
        ResetFrame();
        frameSource_ = source;
        frameContext_.stats.sourceInstanceCount =
            source != nullptr
                ? source->CountGpuSceneInstances()
                : 0u;
        InitializePassExecutionStates(source);
        RefreshPassExecutionStates();
        return true;
    }

    const GpuDrivenSceneUploadStats& GpuDrivenLayer::UploadSceneFrame(
        const GpuDrivenSceneUploadDesc& desc) {

        sceneUploadStats_ = {};
        frameContext_.scene.instanceBuffer = sceneBuffer_;

        if (sceneBuffer_ == nullptr || frameSource_ == nullptr) {
            if (sceneBuffer_ != nullptr) {
                sceneBuffer_->ResetFrame();
                sceneUploadStats_.bufferStats = sceneBuffer_->GetStats();
            }
            if (desc.residency != nullptr) {
                desc.residency->Reset();
            }
            UploadSurfaceGpuSceneFrame(0u, 0u, 0u, 0u, 0u, false);
            return sceneUploadStats_;
        }

        const GpuDrivenSceneSource& source = *frameSource_;
        sceneUploadStats_.sourceInstanceCount =
            source.sourceInstanceCount != 0u
                ? source.sourceInstanceCount
                : source.CountGpuSceneInstances();
        for (size_t passIndex = 0u;
            passIndex < kGpuDrivenPassCount;
            ++passIndex) {

            sceneUploadStats_.passInstanceCounts[passIndex] =
                ClampToUint32(CountPassInstances(source.passes[passIndex]));
        }

        const uint64_t layoutVersion = source.layoutVersion;
        const uint64_t sourceVersion = source.sourceVersion;
        const size_t sourceInstanceCount =
            sceneUploadStats_.sourceInstanceCount;
        const bool residentLayoutMatches =
            desc.residency != nullptr &&
            desc.residency->resident &&
            desc.residency->layoutVersion == layoutVersion &&
            desc.residency->instanceCount == sourceInstanceCount;

        if (sourceInstanceCount == 0u) {
            sceneBuffer_->ResetFrame();
            if (desc.residency != nullptr) {
                desc.residency->Reset();
            }
        } else if (residentLayoutMatches) {
            sceneUploadStats_.reusedResidentFrame = true;
            sceneBuffer_->ReuseFrame(sourceInstanceCount);
            if (desc.residency->sourceVersion != sourceVersion) {
                const bool patched =
                    desc.allowDirtyRangePatching &&
                    source.HasAnyDirtyGpuSceneRanges() &&
                    PatchDirtySceneRanges(*sceneBuffer_, source);
                if (patched) {
                    sceneUploadStats_.patchedDirtyRanges = true;
                } else {
                    UploadFullScene(*sceneBuffer_, source);
                    sceneUploadStats_.uploadedFullScene = true;
                }
            }
        } else {
            UploadFullScene(*sceneBuffer_, source);
            sceneUploadStats_.uploadedFullScene = true;
        }

        sceneUploadStats_.bufferStats = sceneBuffer_->GetStats();
        sceneUploadStats_.sceneResident =
            sourceInstanceCount != 0u &&
            sceneUploadStats_.bufferStats.overflowInstanceCount == 0u &&
            sceneUploadStats_.bufferStats.uploadedInstanceCount ==
                sourceInstanceCount;
        if (desc.residency != nullptr) {
            if (sceneUploadStats_.sceneResident) {
                desc.residency->resident = true;
                desc.residency->layoutVersion = layoutVersion;
                desc.residency->sourceVersion = sourceVersion;
                desc.residency->instanceCount = sourceInstanceCount;
            } else {
                desc.residency->Reset();
            }
        }

        frameContext_.stats.sourceInstanceCount = sourceInstanceCount;
        UploadSurfaceGpuSceneFrame(
            ClampToUint32(sourceInstanceCount),
            ResolvePassBaseIndex(
                source.GetPass(GpuDrivenPassKind::ForwardOpaque)),
            ResolvePassBaseIndex(
                source.GetPass(GpuDrivenPassKind::ForwardDepthAware)),
            ResolvePassBaseIndex(
                source.GetPass(GpuDrivenPassKind::ForwardTransparent)),
            ResolvePassBaseIndex(
                source.GetPass(GpuDrivenPassKind::Shadow)),
            sceneUploadStats_.sceneResident);
        return sceneUploadStats_;
    }

    void GpuDrivenLayer::UploadSurfaceGpuSceneFrame(
        uint32_t instanceCount,
        uint32_t opaqueBaseIndex,
        uint32_t depthAwareBaseIndex,
        uint32_t transparentBaseIndex,
        uint32_t shadowBaseIndex,
        bool sceneResident) {

        frameContext_.scene.instanceBuffer = sceneBuffer_;
        frameContext_.scene.instanceCount = instanceCount;
        frameContext_.scene.opaqueBaseIndex = opaqueBaseIndex;
        frameContext_.scene.depthAwareBaseIndex = depthAwareBaseIndex;
        frameContext_.scene.transparentBaseIndex = transparentBaseIndex;
        frameContext_.scene.shadowBaseIndex = shadowBaseIndex;
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
        const GpuDrivenPassExecutionState& state =
            GetPassExecutionState(pass);
        context.pass = state.visibilityPass;
        context.backend = backend;
        context.scene = &frameContext_.scene;
        context.visibility = &frameContext_.visibility;
        context.commands = &frameContext_.commands;
        context.drawCommandRange =
            frameContext_.drawStream.FindRange(pass, backend);
        if (frameSource_ != nullptr &&
            state.sourceMode != GpuDrivenPassSourceMode::None) {
            context.traditionalIndirect =
                &frameSource_->GetPass(state.sourcePass).traditionalIndirect;
        }
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

    const GpuDrivenDrawCommandStream&
        GpuDrivenLayer::GetDrawCommandStream() const {

        return frameContext_.drawStream;
    }

    const GpuDrivenSceneUploadStats&
        GpuDrivenLayer::GetSceneUploadStats() const {

        return sceneUploadStats_;
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
            state.sourceInstanceCount =
                static_cast<size_t>(passSource.gpuSceneInstanceCount) +
                static_cast<size_t>(
                    passSource.traditionalIndirect.gpuSceneInstanceCount);
            state.hasTraditionalIndirectCommands =
                passSource.traditionalIndirect.HasCommands();
            state.traditionalIndirectCommandCount =
                passSource.traditionalIndirect.CommandCount();
            state.traditionalIndirectInstanceCount =
                passSource.traditionalIndirect.gpuSceneInstanceCount;
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
            const GeometryBackendPolicy policy =
                ResolveGeometryBackendPolicy(state.pass);
            const bool policyAllowsTraditionalIndirect =
                policy.preferred == GeometryBackendKind::GpuDrivenTraditionalVS ||
                (!policy.forcePreferredOnly &&
                    policy.fallback == GeometryBackendKind::GpuDrivenTraditionalVS);

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
                policyAllowsTraditionalIndirect &&
                state.hasTraditionalIndirectCommands &&
                state.traditionalIndirectInstanceCount != 0 &&
                frameContext_.stats.sceneResident &&
                frameContext_.commands.surfaceDrawIndexedArgs != nullptr &&
                frameContext_.commands.surfaceDrawIndexedSignature != nullptr &&
                frameContext_.backendAvailability.traditionalIndirectPipelineReady;
            state.gpuBackendReady =
                state.meshShaderConsumable ||
                state.clusterVsConsumable ||
                state.traditionalIndirectConsumable;
        }
        RebuildDrawCommandStream();
    }

    void GpuDrivenLayer::RebuildDrawCommandStream() {
        frameContext_.drawStream.Reset();
        if (frameSource_ == nullptr) {
            return;
        }

        for (const GpuDrivenPassExecutionState& state :
            frameContext_.passExecution) {

            if (state.sourceMode == GpuDrivenPassSourceMode::None) {
                continue;
            }

            const GpuDrivenPassSource& passSource =
                frameSource_->GetPass(state.sourcePass);
            const GpuDrivenCommandPassLayout& commandLayout =
                frameContext_.commands.layout.GetPass(state.visibilityPass);

            if (state.meshShaderConsumable) {
                GpuDrivenDrawCommandRange range{};
                range.pass = state.pass;
                range.sourcePass = state.sourcePass;
                range.backend = GeometryBackendKind::GpuDrivenMeshShader;
                range.producer = GpuDrivenCommandProducerKind::GpuCommandBuilder;
                range.gpuCommandLayout = &commandLayout;
                range.argumentBuffer = frameContext_.commands.meshDispatchArgs;
                range.commandSignature =
                    frameContext_.commands.meshDispatchSignature;
                range.commandCount = state.drawSeedCount;
                range.instanceCount = state.sourceInstanceCount;
                range.consumable = true;
                range.gpuAuthored = true;
                frameContext_.drawStream.SetRange(range);
            }

            if (state.clusterVsConsumable) {
                GpuDrivenDrawCommandRange range{};
                range.pass = state.pass;
                range.sourcePass = state.sourcePass;
                range.backend = GeometryBackendKind::GpuDrivenClusterVS;
                range.producer = GpuDrivenCommandProducerKind::GpuCommandBuilder;
                range.gpuCommandLayout = &commandLayout;
                range.argumentBuffer = frameContext_.commands.gpuDrawIndexedArgs;
                range.commandSignature =
                    frameContext_.commands.gpuDrawIndexedSignature;
                range.commandCount = state.drawSeedCount;
                range.instanceCount = state.sourceInstanceCount;
                range.consumable = true;
                range.gpuAuthored = true;
                frameContext_.drawStream.SetRange(range);
            }

            if (state.traditionalIndirectConsumable) {
                const GpuDrivenTraditionalIndirectView& view =
                    passSource.traditionalIndirect;
                GpuDrivenDrawCommandRange range{};
                range.pass = state.pass;
                range.sourcePass = state.sourcePass;
                range.backend = GeometryBackendKind::GpuDrivenTraditionalVS;
                range.producer = GpuDrivenCommandProducerKind::CpuScenePlanner;
                range.traditionalIndirect = &view;
                range.argumentBuffer =
                    frameContext_.commands.surfaceDrawIndexedArgs;
                range.commandSignature =
                    frameContext_.commands.surfaceDrawIndexedSignature;
                range.gpuSceneBaseIndex = view.gpuSceneBaseIndex;
                range.commandCount = view.CommandCount();
                range.packetCount =
                    view.executablePacketIndices != nullptr
                        ? view.executablePacketIndices->size()
                        : 0u;
                range.instanceCount = view.gpuSceneInstanceCount;
                range.consumable = true;
                range.gpuAuthored = false;
                frameContext_.drawStream.SetRange(range);
            }
        }
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
