#include "HIKARI_ModelRenderer.h"

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI::MODELR {

    void Submit(const HIKARI::ModelAsset& legacyModelAsset,
        const Transform3D& transform,
        const std::string& materialFxProfileId,
        uint32_t postGroupMask,
        const DirectX::XMFLOAT4(&materialFxParamValues)[4],
        bool materialFxValuesInitialized) {
        MESHRENDERER::SubmitStaticMesh(
            legacyModelAsset,
            transform,
            materialFxProfileId,
            postGroupMask,
            materialFxParamValues,
            materialFxValuesInitialized);
    }

} // namespace HIKARI::MODELR
