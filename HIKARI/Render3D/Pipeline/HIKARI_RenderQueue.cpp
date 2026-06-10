#include "Render3D/Pipeline/HIKARI_RenderQueue.h"

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D {

    namespace {
        const std::vector<const MESHRENDERER::DrawItem*> kEmptyPhase{};

        // DrawItem の MaterialFX 設定から、現在の surface phase を決める。
        RenderPhase ResolvePhase(const MESHRENDERER::DrawItem& item) {
            if (item.hasResolvedMaterialFxProfile &&
                item.resolvedMaterialFxProfile.renderPhase == MaterialFxRenderPhase::DepthAware) {
                return RenderPhase::DepthAware;
            }

            return RenderPhase::Opaque;
        }
    }

    // 現在の surface route が実際に生成する phase だけを保持する。
    void RenderQueue::Clear() {
        opaque_.clear();
        depthAware_.clear();
        transparent_.clear();
    }

    // DrawItem 配列を Opaque / DepthAware / Transparent の実行単位へ分類する。
    void RenderQueue::Build(const std::vector<MESHRENDERER::DrawItem>& items) {
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

    // 指定 phase の描画アイテムを返す。
    const std::vector<const MESHRENDERER::DrawItem*>& RenderQueue::GetPhase(RenderPhase phase) const {
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

    // 指定 phase に実行対象があるかを返す。
    bool RenderQueue::HasPhase(RenderPhase phase) const {
        return !GetPhase(phase).empty();
    }

} // namespace HIKARI::RENDER3D
