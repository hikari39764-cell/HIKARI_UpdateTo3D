#include "Render3D/Pipeline/HIKARI_CpuRenderQueue.h"

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D {

    namespace {
        const std::vector<const MESHRENDERER::DrawItem*> kEmptyPhase{};

        // DrawItem の MaterialFX 設定から CPU 実行用の phase を決める。
        RenderPhase ResolvePhase(const MESHRENDERER::DrawItem& item) {
            if (item.hasResolvedMaterialFxProfile &&
                item.resolvedMaterialFxProfile.renderPhase == MaterialFxRenderPhase::DepthAware) {
                return RenderPhase::DepthAware;
            }

            return RenderPhase::Opaque;
        }
    }

    // CPU で投入された DrawItem だけを phase 別に保持する。
    void CpuRenderQueue::Clear() {
        opaque_.clear();
        depthAware_.clear();
        transparent_.clear();
    }

    // GPU-driven resident scene ではなく、CPU 側 DrawItem 配列を実行 phase に分配する。
    void CpuRenderQueue::Build(const std::vector<MESHRENDERER::DrawItem>& items) {
        Clear();

        for (const MESHRENDERER::DrawItem& item : items) {
            switch (ResolvePhase(item)) {
            case RenderPhase::DepthAware:
                depthAware_.push_back(&item);
                break;
            case RenderPhase::Transparent:
                transparent_.push_back(&item);
                break;
            case RenderPhase::Opaque:
            default:
                opaque_.push_back(&item);
                break;
            }
        }
    }

    // 指定 phase の CPU 描画アイテムを返す。
    const std::vector<const MESHRENDERER::DrawItem*>& CpuRenderQueue::GetPhase(RenderPhase phase) const {
        switch (phase) {
        case RenderPhase::Opaque:
            return opaque_;
        case RenderPhase::DepthAware:
            return depthAware_;
        case RenderPhase::Transparent:
            return transparent_;
        default:
            return kEmptyPhase;
        }
    }

    // 指定 phase に CPU 描画対象があるかを返す。
    bool CpuRenderQueue::HasPhase(RenderPhase phase) const {
        return !GetPhase(phase).empty();
    }

} // namespace HIKARI::RENDER3D
