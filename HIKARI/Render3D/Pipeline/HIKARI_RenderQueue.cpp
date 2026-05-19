#include "Render3D/Pipeline/HIKARI_RenderQueue.h"

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D {

    namespace {
        const std::vector<const MESHRENDERER::DrawItem*> kEmptyPhase{};

        RenderPhase ResolvePhase(const MESHRENDERER::DrawItem& item) {
            if (item.hasResolvedMaterialFxProfile &&
                item.resolvedMaterialFxProfile.renderPhase == MaterialFxRenderPhase::SceneDepth) {
                return RenderPhase::DepthAware;
            }

            return RenderPhase::Opaque;
        }
    }

    void RenderQueue::Clear() {
        opaque_.clear();
        depthAware_.clear();
        sceneColorAware_.clear();
        transparent_.clear();
        distortion_.clear();
        overlay_.clear();
        debug_.clear();
    }

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

    bool RenderQueue::HasPhase(RenderPhase phase) const {
        return !GetPhase(phase).empty();
    }

} // namespace HIKARI::RENDER3D
