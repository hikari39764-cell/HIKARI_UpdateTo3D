#include "Render3D/Cluster/HIKARI_ClusterMainline.h"

#include "Render3D/Core/HIKARI_SurfaceGpuSceneFrameBuffer.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {

        uint32_t ToSurfaceGpuSceneFlag(
            RUNTIME::SurfaceGpuSceneInstanceFlags flag) {
            return static_cast<uint32_t>(flag);
        }

        constexpr uint32_t kRequiredClusterResourceFlags =
            static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneResourceFlags::ClusterGeometry) |
            static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneResourceFlags::ClusterGeometryShaderVisible) |
            static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneResourceFlags::ClusterGeometrySurfaceRange);

        void CountEligibility(
            ClusterMainlineOwnershipStats& out,
            ClusterMainlineEligibility eligibility) {

            switch (eligibility) {
            case ClusterMainlineEligibility::Eligible:
                ++out.eligibleCommandCount;
                break;
            case ClusterMainlineEligibility::MissingContext:
                ++out.rejectContextCommandCount;
                break;
            case ClusterMainlineEligibility::MissingMainline:
                ++out.rejectMainlineCommandCount;
                break;
            case ClusterMainlineEligibility::WrongBackend:
                ++out.rejectBackendCommandCount;
                break;
            case ClusterMainlineEligibility::UnsupportedTransparent:
                ++out.rejectTransparentCommandCount;
                break;
            case ClusterMainlineEligibility::UnsupportedMaterialFx:
                ++out.rejectMaterialFxCommandCount;
                break;
            case ClusterMainlineEligibility::InvalidGpuSceneRange:
                ++out.rejectRangeCommandCount;
                break;
            case ClusterMainlineEligibility::MissingInstanceResource:
                ++out.rejectInstanceResourceCommandCount;
                break;
            case ClusterMainlineEligibility::UnsupportedInstanceFlag:
                ++out.rejectInstanceFlagCommandCount;
                break;
            case ClusterMainlineEligibility::MissingMaterialPatch:
                ++out.rejectMaterialPatchCommandCount;
                break;
            }
        }

        bool IsValidCommandInstanceRange(
            const RUNTIME::SurfaceDrawCommand& command,
            size_t frameInstanceCount,
            size_t& outBegin,
            size_t& outCount) {

            if (command.firstGpuSceneInstanceIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
                command.gpuSceneInstanceCount == 0u) {
                return false;
            }

            const size_t begin = static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            const size_t count = static_cast<size_t>(command.gpuSceneInstanceCount);
            if (begin > frameInstanceCount || count > frameInstanceCount - begin) {
                return false;
            }

            outBegin = begin;
            outCount = count;
            return true;
        }

        bool OwnsCommandByFrame(
            const ClusterMainlineFrame& frame,
            const RUNTIME::SurfaceDrawCommand& command) {

            size_t begin = 0;
            size_t count = 0;
            if (!IsValidCommandInstanceRange(
                command,
                frame.sourceInstanceCount,
                begin,
                count)) {
                return false;
            }
            return frame.OwnsInstanceRange(begin, count);
        }

    } // namespace

    bool ClusterMainlinePolicy::OwnsForwardOpaque() const {
        return cluster.forwardReady;
    }

    bool ClusterMainlinePolicy::OwnsGeometryAux() const {
        return cluster.geometryAuxReady;
    }

    bool ClusterMainlinePolicy::OwnsPass(ClusterMainlinePass pass) const {
        return pass == ClusterMainlinePass::GeometryAux
            ? OwnsGeometryAux()
            : OwnsForwardOpaque();
    }

    ClusterMainlineState ResolveClusterMainlineState(
        const ClusterMainlineSignals& signals) {

        ClusterMainlineState state{};
        state.hasDrawSeeds =
            signals.candidateInstanceCount > 0 &&
            signals.drawSeedCount > 0;
        state.overflowBlocked =
            signals.overflowInstanceCount != 0;
        state.baseReady =
            signals.gpuCullReady &&
            signals.drawArgsReady &&
            signals.commandSignatureReady &&
            state.hasDrawSeeds &&
            !state.overflowBlocked;
        state.forwardReady =
            state.baseReady &&
            signals.forwardPipelineReady;
        // GeometryAux は SSAO などの補助 pass。Forward 本線が成立した時だけ所有権を持つ。
        state.geometryAuxReady =
            state.forwardReady &&
            signals.geometryAuxPipelineReady;
        return state;
    }

    ClusterMainlinePolicy ResolveClusterMainlinePolicy(
        const ClusterMainlineSignals& signals) {

        ClusterMainlinePolicy policy{};
        policy.cluster = ResolveClusterMainlineState(signals);
        return policy;
    }

    ClusterMainlineEligibility EvaluateClusterMainlineInstance(
        const RUNTIME::SurfaceGpuSceneInstance& instance) {

        const uint32_t transparentFlag =
            ToSurfaceGpuSceneFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::Transparent);
        const uint32_t materialFxFlag =
            ToSurfaceGpuSceneFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::MaterialFx);
        const uint32_t clusterMainlineFlag =
            ToSurfaceGpuSceneFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::ClusterMainline);

        if (instance.geometryBackend !=
            static_cast<uint32_t>(RUNTIME::SurfaceGeometryBackend::ClusterGeometry)) {
            return ClusterMainlineEligibility::WrongBackend;
        }
        if ((instance.resourceFlags & kRequiredClusterResourceFlags) != kRequiredClusterResourceFlags ||
            instance.clusterGeometrySrvDescriptorIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
            instance.clusterRangeIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
            instance.clusterRangeCount == 0u ||
            instance.clusterIndexCount == 0u) {
            return ClusterMainlineEligibility::MissingInstanceResource;
        }
        if ((instance.flags & transparentFlag) != 0u) {
            return ClusterMainlineEligibility::UnsupportedTransparent;
        }
        if ((instance.flags & materialFxFlag) != 0u ||
            instance.fxFlags != 0u) {
            return ClusterMainlineEligibility::UnsupportedMaterialFx;
        }
        if ((instance.flags & clusterMainlineFlag) == 0u) {
            return ClusterMainlineEligibility::MissingMainline;
        }
        return ClusterMainlineEligibility::Eligible;
    }

    ClusterMainlineEligibility EvaluateClusterMainlineCommand(
        const RUNTIME::SurfaceDrawCommand& command,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset) {

        if (instances == nullptr || gpuSceneFrameBuffer == nullptr) {
            return ClusterMainlineEligibility::MissingContext;
        }
        if (command.geometryBackend != RUNTIME::SurfaceGeometryBackend::ClusterGeometry) {
            return ClusterMainlineEligibility::WrongBackend;
        }
        if (command.transparent) {
            return ClusterMainlineEligibility::UnsupportedTransparent;
        }
        if (command.firstGpuSceneInstanceIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
            command.gpuSceneInstanceCount == 0u) {
            return ClusterMainlineEligibility::InvalidGpuSceneRange;
        }

        const size_t begin = static_cast<size_t>(command.firstGpuSceneInstanceIndex);
        const size_t count = static_cast<size_t>(command.gpuSceneInstanceCount);
        if (begin > instances->size() || count > instances->size() - begin) {
            return ClusterMainlineEligibility::InvalidGpuSceneRange;
        }

        for (size_t i = 0; i < count; ++i) {
            const ClusterMainlineEligibility instanceEligibility =
                EvaluateClusterMainlineInstance((*instances)[begin + i]);
            if (instanceEligibility != ClusterMainlineEligibility::Eligible) {
                return instanceEligibility;
            }

            const size_t absoluteGpuSceneIndex =
                surfaceGpuSceneBaseOffset + begin + i;
            if (!gpuSceneFrameBuffer->HasMaterialDataIndex(absoluteGpuSceneIndex)) {
                return ClusterMainlineEligibility::MissingMaterialPatch;
            }
        }
        if (!command.clusterMainlineEligible) {
            return ClusterMainlineEligibility::MissingMainline;
        }
        return ClusterMainlineEligibility::Eligible;
    }

    bool IsClusterMainlineInstanceEligible(
        const RUNTIME::SurfaceGpuSceneInstance& instance) {

        return EvaluateClusterMainlineInstance(instance) ==
            ClusterMainlineEligibility::Eligible;
    }

    bool IsClusterMainlineCommandEligible(
        const RUNTIME::SurfaceDrawCommand& command,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset) {

        return EvaluateClusterMainlineCommand(
            command,
            instances,
            gpuSceneFrameBuffer,
            surfaceGpuSceneBaseOffset) == ClusterMainlineEligibility::Eligible;
    }

    void ClusterMainlineFrame::Reset() {
        policy = {};
        instances = nullptr;
        gpuSceneFrameBuffer = nullptr;
        surfaceGpuSceneBaseOffset = 0;
        candidateInstanceMask.clear();
        ownedInstanceMask.clear();
        sourceInstanceCount = 0;
        candidateInstanceCount = 0;
        ownedInstanceCount = 0;
        legacyInstanceCount = 0;
    }

    bool ClusterMainlineFrame::IsCandidateInstance(size_t instanceIndex) const {
        return instanceIndex < candidateInstanceMask.size() &&
            candidateInstanceMask[instanceIndex] != 0u;
    }

    bool ClusterMainlineFrame::OwnsInstance(size_t instanceIndex) const {
        return instanceIndex < ownedInstanceMask.size() &&
            ownedInstanceMask[instanceIndex] != 0u;
    }

    bool ClusterMainlineFrame::OwnsInstanceRange(
        size_t firstInstanceIndex,
        size_t instanceCount) const {

        if (instanceCount == 0 ||
            firstInstanceIndex > ownedInstanceMask.size() ||
            instanceCount > ownedInstanceMask.size() - firstInstanceIndex) {
            return false;
        }

        for (size_t i = 0; i < instanceCount; ++i) {
            if (ownedInstanceMask[firstInstanceIndex + i] == 0u) {
                return false;
            }
        }
        return true;
    }

    bool OwnsOpaqueCommand(
        const ClusterMainlinePolicy& policy,
        ClusterMainlinePass pass,
        const RUNTIME::SurfaceDrawCommand& command,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset) {

        return
            policy.OwnsPass(pass) &&
            IsClusterMainlineCommandEligible(
                command,
                instances,
                gpuSceneFrameBuffer,
                surfaceGpuSceneBaseOffset);
    }

    bool ShouldOwnSurfaceCommand(
        const ClusterMainlineCommandContext& ctx,
        const RUNTIME::SurfaceDrawCommand& command) {

        if (!ctx.opaqueExecutionKind ||
            !ctx.policy.OwnsPass(ctx.pass)) {
            return false;
        }

        if (ctx.frame != nullptr) {
            return OwnsCommandByFrame(*ctx.frame, command);
        }

        return OwnsOpaqueCommand(
            ctx.policy,
            ctx.pass,
            command,
            ctx.instances,
            ctx.gpuSceneFrameBuffer,
            ctx.surfaceGpuSceneBaseOffset);
    }

    bool ShouldUploadSurfaceIndirectCommand(
        const RUNTIME::SurfaceDrawCommand& command,
        const void* userData) {

        const auto* ctx =
            static_cast<const ClusterMainlineCommandContext*>(userData);
        return ctx == nullptr || !ShouldOwnSurfaceCommand(*ctx, command);
    }

    void BuildStaticOpaqueFrame(
        const ClusterMainlinePolicy& policy,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset,
        ClusterMainlineFrame& outFrame) {

        outFrame.Reset();
        outFrame.policy = policy;
        outFrame.instances = instances;
        outFrame.gpuSceneFrameBuffer = gpuSceneFrameBuffer;
        outFrame.surfaceGpuSceneBaseOffset = surfaceGpuSceneBaseOffset;

        if (instances == nullptr || instances->empty()) {
            return;
        }

        outFrame.sourceInstanceCount = instances->size();
        outFrame.candidateInstanceMask.assign(instances->size(), 0u);
        outFrame.ownedInstanceMask.assign(instances->size(), 0u);
        if (gpuSceneFrameBuffer == nullptr) {
            outFrame.legacyInstanceCount = outFrame.sourceInstanceCount;
            return;
        }

        for (size_t i = 0; i < instances->size(); ++i) {
            const size_t absoluteGpuSceneIndex = surfaceGpuSceneBaseOffset + i;
            const bool candidate =
                EvaluateClusterMainlineInstance((*instances)[i]) ==
                    ClusterMainlineEligibility::Eligible &&
                gpuSceneFrameBuffer->HasMaterialDataIndex(absoluteGpuSceneIndex);
            if (candidate) {
                outFrame.candidateInstanceMask[i] = 1u;
                ++outFrame.candidateInstanceCount;
            }
            if (candidate && policy.OwnsForwardOpaque()) {
                outFrame.ownedInstanceMask[i] = 1u;
                ++outFrame.ownedInstanceCount;
            }
        }
        outFrame.legacyInstanceCount =
            outFrame.sourceInstanceCount - outFrame.ownedInstanceCount;
    }

    ClusterMainlineOwnershipStats BuildOpaqueOwnershipStats(
        const ClusterMainlinePolicy& policy,
        const RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount,
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
        const CORE::SurfaceGpuSceneFrameBuffer* gpuSceneFrameBuffer,
        size_t surfaceGpuSceneBaseOffset) {

        ClusterMainlineOwnershipStats out{};
        if (commands == nullptr || commandCount == 0) {
            return out;
        }

        for (size_t i = 0; i < commandCount; ++i) {
            const RUNTIME::SurfaceDrawCommand& command = commands[i];
            CountEligibility(
                out,
                EvaluateClusterMainlineCommand(
                    command,
                    instances,
                    gpuSceneFrameBuffer,
                    surfaceGpuSceneBaseOffset));

            if (OwnsOpaqueCommand(
                policy,
                ClusterMainlinePass::ForwardOpaque,
                command,
                instances,
                gpuSceneFrameBuffer,
                surfaceGpuSceneBaseOffset)) {
                ++out.ownedCommandCount;
                out.ownedPacketCount += command.packetCount;
            } else {
                ++out.legacyCommandCount;
                out.legacyPacketCount += command.packetCount;
            }

            if (OwnsOpaqueCommand(
                policy,
                ClusterMainlinePass::GeometryAux,
                command,
                instances,
                gpuSceneFrameBuffer,
                surfaceGpuSceneBaseOffset)) {
                ++out.geometryAuxCommandCount;
                out.geometryAuxPacketCount += command.packetCount;
            }
        }

        out.bypassedLegacyCommandCount = out.ownedCommandCount;
        out.bypassedLegacyPacketCount = out.ownedPacketCount;
        return out;
    }

    ClusterMainlineOwnershipStats BuildOpaqueOwnershipStats(
        const ClusterMainlineFrame& frame,
        const RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount) {

        ClusterMainlineOwnershipStats out{};
        if (commands == nullptr || commandCount == 0) {
            return out;
        }

        for (size_t i = 0; i < commandCount; ++i) {
            const RUNTIME::SurfaceDrawCommand& command = commands[i];
            size_t begin = 0;
            size_t count = 0;
            if (!IsValidCommandInstanceRange(
                command,
                frame.sourceInstanceCount,
                begin,
                count)) {
                ++out.rejectRangeCommandCount;
                ++out.legacyCommandCount;
                out.legacyPacketCount += command.packetCount;
                continue;
            }

            const bool forwardOwned =
                frame.policy.OwnsForwardOpaque() &&
                frame.OwnsInstanceRange(begin, count);
            if (forwardOwned) {
                ++out.eligibleCommandCount;
                ++out.ownedCommandCount;
                out.ownedPacketCount += command.packetCount;
            } else {
                ++out.rejectMainlineCommandCount;
                ++out.legacyCommandCount;
                out.legacyPacketCount += command.packetCount;
            }

            if (frame.policy.OwnsGeometryAux() &&
                frame.OwnsInstanceRange(begin, count)) {
                ++out.geometryAuxCommandCount;
                out.geometryAuxPacketCount += command.packetCount;
            }
        }

        out.bypassedLegacyCommandCount = out.ownedCommandCount;
        out.bypassedLegacyPacketCount = out.ownedPacketCount;
        return out;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
