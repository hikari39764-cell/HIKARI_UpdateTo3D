#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkReadiness.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    bool GpuDrivenWorkPolicy::OwnsForwardOpaque() const {
        return work.forwardReady;
    }

    bool GpuDrivenWorkPolicy::OwnsGeometryAux() const {
        return work.geometryAuxReady;
    }

    bool GpuDrivenWorkPolicy::OwnsPass(GpuDrivenWorkPass pass) const {
        return pass == GpuDrivenWorkPass::GeometryAux
            ? OwnsGeometryAux()
            : OwnsForwardOpaque();
    }

    GpuDrivenWorkReadiness ResolveGpuDrivenWorkReadiness(
        const GpuDrivenWorkSignals& signals) {

        GpuDrivenWorkReadiness state{};
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
        // GeometryAux は Forward と同じ GPU work frame を読むため、
        // Forward が成立してから追加パイプラインの可用性だけを見る。
        state.geometryAuxReady =
            state.forwardReady &&
            signals.geometryAuxPipelineReady;
        return state;
    }

    GpuDrivenWorkPolicy ResolveGpuDrivenWorkPolicy(
        const GpuDrivenWorkSignals& signals) {

        GpuDrivenWorkPolicy policy{};
        policy.work = ResolveGpuDrivenWorkReadiness(signals);
        return policy;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
