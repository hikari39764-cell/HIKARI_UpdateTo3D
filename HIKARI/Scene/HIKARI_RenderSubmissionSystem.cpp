#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Assets/HIKARI_Assets.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

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

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};

    const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() {
        return sDebugStats_;
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        (void)frame;

        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.submittedDrawItemCount = 0;
        sDebugStats_.fallbackWireCount = 0;
        sDebugStats_.items.clear();

        world.ForEachObjectWith<ModelComponent>([](GameObject& object, ModelComponent& model) {
            if (!model.IsVisible()) {
                return;
            }

            ASSET::AssetRegistry& registry = ASSET::GetGlobalAssetRegistry();
            const ASSET::ModelAsset* asset = registry.FindModel(model.GetModelHandle());
            if (asset && asset->state == ASSET::AssetState::Ready && !asset->primitives.empty()) {
                ++sDebugStats_.submittedModelCount;

                const MATH::Mat4 entityWorld = object.Transform().GetWorldMatrix();
                for (const ASSET::ModelAsset::Primitive& primitive : asset->primitives) {
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
                    item.castShadow = model.CastShadow();
                    item.receiveShadow = model.ReceiveShadow();
                    item.renderLayerMask = model.GetRenderLayerMask();
                    item.postGroupMask = model.GetPostGroupMask();
                    item.materialFxProfileId = model.GetMaterialFxProfileId();
                    for (size_t i = 0; i < item.materialFxUser.size(); ++i) {
                        item.materialFxUser[i] = model.GetMaterialFxParamValues()[i];
                    }
                    item.materialFxValuesInitialized = model.AreMaterialFxValuesInitialized();
                    MESHRENDERER::SubmitStaticDrawItem(item);

                    SubmittedDrawItemDebugInfo debugInfo{};
                    debugInfo.gpuMeshId = item.gpuMeshId;
                    debugInfo.material = item.material;
                    debugInfo.modelState = asset->state;
                    debugInfo.postGroupMask = item.postGroupMask;
                    if (const ASSET::MaterialAsset* material = registry.FindMaterial(item.material)) {
                        debugInfo.hasBaseColorTexture = material->baseColorTexture.IsValid() ? 1u : 0u;
                        debugInfo.hasNormalTexture = material->normalTexture.IsValid() ? 1u : 0u;
                        debugInfo.hasOrmTexture = material->ormTexture.IsValid() ? 1u : 0u;
                        debugInfo.hasEmissiveTexture = material->emissiveTexture.IsValid() ? 1u : 0u;
                    }
                    sDebugStats_.items.push_back(debugInfo);
                    ++sDebugStats_.submittedDrawItemCount;
                }
            } else {
                ++sDebugStats_.fallbackWireCount;

                RENDERER3D::WireCube cube{};
                cube.transform = object.Transform();
                cube.size = 1.0f;
                cube.rgba = 0x66CCFFFF;
                RENDERER3D::SubmitWireCube(cube);
            }
        });
    }

} // namespace HIKARI
