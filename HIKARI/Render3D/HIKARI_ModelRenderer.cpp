#include "HIKARI_ModelRenderer.h"

#include "Render3D/Core/HIKARI_MeshRenderer.h"

namespace HIKARI::MODELR {

    void SubmitModelComponent(
        ASSET::AssetRegistry& registry,
        ASSET::AssetHandle<ASSET::ModelAsset> model,
        const Transform3D& world,
        bool visible,
        bool castShadow,
        bool receiveShadow,
        uint32_t renderLayerMask,
        const std::string& materialFxProfileId,
        uint32_t postGroupMask,
        const DirectX::XMFLOAT4(&materialFxParamValues)[4],
        bool materialFxValuesInitialized) {
        const ASSET::ModelAsset* resolved = registry.FindModel(model);
        if (resolved == nullptr || resolved->state == ASSET::AssetState::Failed) {
            return;
        }

        MESHRENDERER::StaticModelSubmission submission{};
        submission.model = model;
        submission.world = world;
        submission.visible = visible;
        submission.castShadow = castShadow;
        submission.receiveShadow = receiveShadow;
        submission.renderLayerMask = renderLayerMask;
        submission.postGroupMask = postGroupMask;
        submission.materialFxProfileId = materialFxProfileId;
        for (size_t i = 0; i < 4; ++i) {
            submission.materialFxUser[i] = materialFxParamValues[i];
        }
        submission.materialFxValuesInitialized = materialFxValuesInitialized;
        MESHRENDERER::SubmitStaticModel(registry, submission);
    }

} // namespace HIKARI::MODELR
