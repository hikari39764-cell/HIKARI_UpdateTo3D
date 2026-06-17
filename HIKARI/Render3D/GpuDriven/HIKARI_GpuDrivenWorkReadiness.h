#pragma once

#include <cstddef>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    enum class GpuDrivenWorkPass {
        ForwardOpaque,
        GeometryAux,
    };

    struct GpuDrivenWorkSignals {
        bool gpuCullReady = false;
        bool drawArgsReady = false;
        bool commandSignatureReady = false;
        bool forwardPipelineReady = false;
        bool geometryAuxPipelineReady = false;
        size_t candidateInstanceCount = 0;
        size_t drawSeedCount = 0;
        size_t overflowInstanceCount = 0;
    };

    struct GpuDrivenWorkReadiness {
        bool hasDrawSeeds = false;
        bool overflowBlocked = false;
        bool baseReady = false;
        bool forwardReady = false;
        bool geometryAuxReady = false;
    };

    struct GpuDrivenWorkPolicy {
        GpuDrivenWorkReadiness work{};

        bool OwnsForwardOpaque() const;
        bool OwnsGeometryAux() const;
        bool OwnsPass(GpuDrivenWorkPass pass) const;
    };

    struct GpuDrivenWorkOwnershipStats {
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

    GpuDrivenWorkReadiness ResolveGpuDrivenWorkReadiness(
        const GpuDrivenWorkSignals& signals);
    GpuDrivenWorkPolicy ResolveGpuDrivenWorkPolicy(
        const GpuDrivenWorkSignals& signals);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
