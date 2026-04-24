#include "HIKARI_ModelRenderer.h"

#include "Render3D/Core/HIKARI_MeshRenderer.h"

namespace HIKARI::MODELR {

    namespace {
        MATH::Mat4 ToMat4(const DirectX::XMFLOAT4X4& matrix) {
            MATH::Mat4 out{};
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    out.m[c][r] = matrix.m[c][r];
                }
            }
            return out;
        }
    }

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
        if (!visible) {
            return;
        }

        const ASSET::ModelAsset* resolved = registry.FindModel(model);
        if (resolved == nullptr || resolved->state != ASSET::AssetState::Ready) {
            return;
        }

        const MATH::Mat4 entityWorld = world.GetWorldMatrix();
        for (const ASSET::ModelAsset::Primitive& primitive : resolved->primitives) {
            const ASSET::MeshAsset* mesh = registry.FindMesh(primitive.mesh);
            if (mesh == nullptr || mesh->gpuMeshId == 0) {
                continue;
            }

            MESHRENDERER::StaticModelDrawItem item{};
            item.registry = &registry;
            item.gpuMeshId = mesh->gpuMeshId;
            item.material = primitive.material;
            item.world = ToMat4(primitive.localTransform) * entityWorld;
            item.normalMatrix = item.world;
            item.normalMatrix.m[3][0] = 0.0f;
            item.normalMatrix.m[3][1] = 0.0f;
            item.normalMatrix.m[3][2] = 0.0f;
            item.castShadow = castShadow;
            item.receiveShadow = receiveShadow;
            item.renderLayerMask = renderLayerMask;
            item.postGroupMask = postGroupMask;
            item.materialFxProfileId = materialFxProfileId;
            for (size_t i = 0; i < item.materialFxUser.size(); ++i) {
                item.materialFxUser[i] = materialFxParamValues[i];
            }
            item.materialFxValuesInitialized = materialFxValuesInitialized;
            MESHRENDERER::SubmitStaticDrawItem(item);
        }
    }

} // namespace HIKARI::MODELR
