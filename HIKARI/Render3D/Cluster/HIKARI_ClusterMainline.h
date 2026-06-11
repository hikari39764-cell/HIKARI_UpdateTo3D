#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::CORE {
    class SurfaceGpuSceneFrameBuffer;
}

namespace HIKARI::RENDER3D::CLUSTER {

    enum class ClusterMainlinePass {
        ForwardOpaque,
        GeometryAux,
    };

    enum class ClusterMainlineEligibility {
        Eligible,
        MissingContext,
        MissingMainline,
        WrongBackend,
        UnsupportedTransparent,
        UnsupportedMaterialFx,
        InvalidGpuSceneRange,
        MissingInstanceResource,
        UnsupportedInstanceFlag,
        MissingMaterialPatch,
    };

    struct ClusterMainlineSignals {
        bool gpuCullReady = false;
        bool drawArgsReady = false;
        bool commandSignatureReady = false;
        bool forwardPipelineReady = false;
        bool geometryAuxPipelineReady = false;
        size_t candidateInstanceCount = 0;
        size_t drawSeedCount = 0;
        size_t overflowInstanceCount = 0;
    };

    struct ClusterMainlineState {
        bool hasDrawSeeds = false;
        bool overflowBlocked = false;
        bool baseReady = false;
        bool forwardReady = false;
        bool geometryAuxReady = false;
    };

    struct ClusterMainlinePolicy {
        ClusterMainlineState cluster{};

        bool OwnsForwardOpaque() const;
        bool OwnsGeometryAux() const;
        bool OwnsPass(ClusterMainlinePass pass) const;
    };

    struct ClusterMainlineFrame;

    struct ClusterMainlineCommandContext {
        ClusterMainlinePolicy policy{};
        const ClusterMainlineFrame* frame = nullptr;
        bool opaqueExecutionKind = false;
        ClusterMainlinePass pass = ClusterMainlinePass::ForwardOpaque;
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances = nullptr;
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer = nullptr;
        size_t surfaceGpuSceneBaseOffset = 0;
    };

    struct ClusterMainlineOwnershipStats {
        size_t ownedCommandCount = 0;
        size_t ownedPacketCount = 0;
        size_t geometryAuxCommandCount = 0;
        size_t geometryAuxPacketCount = 0;
        size_t legacyCommandCount = 0;
        size_t legacyPacketCount = 0;
        size_t bypassedLegacyCommandCount = 0;
        size_t bypassedLegacyPacketCount = 0;

        size_t eligibleCommandCount = 0;
        size_t rejectContextCommandCount = 0;
        size_t rejectMainlineCommandCount = 0;
        size_t rejectBackendCommandCount = 0;
        size_t rejectTransparentCommandCount = 0;
        size_t rejectMaterialFxCommandCount = 0;
        size_t rejectRangeCommandCount = 0;
        size_t rejectInstanceResourceCommandCount = 0;
        size_t rejectInstanceFlagCommandCount = 0;
        size_t rejectMaterialPatchCommandCount = 0;
    };

    struct ClusterMainlineFrame {
        ClusterMainlinePolicy policy{};
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances = nullptr;
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer = nullptr;
        size_t surfaceGpuSceneBaseOffset = 0;
        std::vector<uint8_t> candidateInstanceMask{};
        std::vector<uint8_t> ownedInstanceMask{};
        size_t sourceInstanceCount = 0;
        size_t candidateInstanceCount = 0;
        size_t ownedInstanceCount = 0;
        size_t legacyInstanceCount = 0;

        void Reset();
        bool IsCandidateInstance(size_t instanceIndex) const;
        bool OwnsInstance(size_t instanceIndex) const;
        bool OwnsInstanceRange(size_t firstInstanceIndex, size_t instanceCount) const;
    };

    ClusterMainlineState ResolveClusterMainlineState(
        const ClusterMainlineSignals& signals);
    ClusterMainlinePolicy ResolveClusterMainlinePolicy(
        const ClusterMainlineSignals& signals);

    ClusterMainlineEligibility EvaluateClusterMainlineInstance(
        const RUNTIME::SurfaceGpuSceneInstance& instance);
    ClusterMainlineEligibility EvaluateClusterMainlineCommand(
        const RUNTIME::SurfaceDrawCommand& command,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset);

    bool IsClusterMainlineInstanceEligible(
        const RUNTIME::SurfaceGpuSceneInstance& instance);
    bool IsClusterMainlineCommandEligible(
        const RUNTIME::SurfaceDrawCommand& command,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset);
    bool OwnsOpaqueCommand(
        const ClusterMainlinePolicy& policy,
        ClusterMainlinePass pass,
        const RUNTIME::SurfaceDrawCommand& command,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset);
    bool ShouldOwnSurfaceCommand(
        const ClusterMainlineCommandContext& ctx,
        const RUNTIME::SurfaceDrawCommand& command);
    bool ShouldUploadSurfaceIndirectCommand(
        const RUNTIME::SurfaceDrawCommand& command,
        const void* userData);

    void BuildStaticOpaqueFrame(
        const ClusterMainlinePolicy& policy,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset,
        ClusterMainlineFrame& outFrame);

    ClusterMainlineOwnershipStats BuildOpaqueOwnershipStats(
        const ClusterMainlinePolicy& policy,
        const RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset);
    ClusterMainlineOwnershipStats BuildOpaqueOwnershipStats(
        const ClusterMainlineFrame& frame,
        const RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount);

} // namespace HIKARI::RENDER3D::CLUSTER
