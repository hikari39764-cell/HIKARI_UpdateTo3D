#include "Render3D/Pipeline/HIKARI_RenderQueue.h"

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D {

    namespace {
        const std::vector<const MESHRENDERER::DrawItem*> kEmptyPhase{};
		// 描画アイテムの情報から、どのレンダリングフェーズに属するかを決定する。
        RenderPhase ResolvePhase(const MESHRENDERER::DrawItem& item) {
            if (item.hasResolvedMaterialFxProfile &&
                item.resolvedMaterialFxProfile.renderPhase == MaterialFxRenderPhase::SceneDepth) {
                return RenderPhase::DepthAware;
            }

            return RenderPhase::Opaque;
        }
    }
	// 各フェーズの描画アイテムのリストをクリアする。
    void RenderQueue::Clear() {
        opaque_.clear();
        depthAware_.clear();
        sceneColorAware_.clear();
        transparent_.clear();
        distortion_.clear();
        overlay_.clear();
        debug_.clear();
    }
	// 描画アイテムのリストを受け取り、各アイテムを適切なレンダリングフェーズのリストに振り分ける。
    void RenderQueue::Build(const std::vector<MESHRENDERER::DrawItem>& items) {
        Clear();

        for (const MESHRENDERER::DrawItem& item : items) {
            switch (ResolvePhase(item)) {
            case RenderPhase::DepthAware:
                depthAware_.push_back(&item);
                break;
            case RenderPhase::SceneColorAware:
                sceneColorAware_.push_back(&item);
                break;
            case RenderPhase::Transparent:
                transparent_.push_back(&item);
                break;
            case RenderPhase::Distortion:
                distortion_.push_back(&item);
                break;
            case RenderPhase::Overlay:
                overlay_.push_back(&item);
                break;
            case RenderPhase::Debug:
                debug_.push_back(&item);
                break;
            case RenderPhase::Opaque:
            default:
                opaque_.push_back(&item);
                break;
            }
        }
    }
	// 指定されたレンダリングフェーズに属する描画アイテムのリストを返す。フェーズが存在しない場合は空のリストを返す。
    const std::vector<const MESHRENDERER::DrawItem*>& RenderQueue::GetPhase(RenderPhase phase) const {
        switch (phase) {
        case RenderPhase::Opaque:
            return opaque_;
        case RenderPhase::DepthAware:
            return depthAware_;
        case RenderPhase::SceneColorAware:
            return sceneColorAware_;
        case RenderPhase::Transparent:
            return transparent_;
        case RenderPhase::Distortion:
            return distortion_;
        case RenderPhase::Overlay:
            return overlay_;
        case RenderPhase::Debug:
            return debug_;
        default:
            return kEmptyPhase;
        }
    }
	// 指定されたレンダリングフェーズに描画アイテムが存在するかどうかを返す。
    bool RenderQueue::HasPhase(RenderPhase phase) const {
        return !GetPhase(phase).empty();
    }

} // namespace HIKARI::RENDER3D
