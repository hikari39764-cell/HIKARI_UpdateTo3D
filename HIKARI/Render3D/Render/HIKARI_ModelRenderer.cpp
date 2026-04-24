#include "Render3D/Render/HIKARI_ModelRenderer.h"

#include <vector>

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_ModelAsset.h"

namespace HIKARI::MODELRENDERER {

    namespace {
        std::vector<ModelRenderItem> gQueue;
    }

    void Reset() {
        gQueue.clear();
        MESHRENDERER::Reset();
    }

    void SubmitModel(const ModelRenderItem& item) {
        if (!item.model) {
            return;
        }
        gQueue.push_back(item);
    }

    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment) {
        for (const ModelRenderItem& item : gQueue) {
            if (!item.model) {
                continue;
            }
            MESHRENDERER::SubmitStaticMesh(
                *item.model,
                item.worldTransform,
                item.materialFxProfileId,
                item.postGroupMask,
                item.materialFxParamValues,
                item.materialFxValuesInitialized);
        }

        MESHRENDERER::RenderAll(camera, environment);
        gQueue.clear();
    }

}
