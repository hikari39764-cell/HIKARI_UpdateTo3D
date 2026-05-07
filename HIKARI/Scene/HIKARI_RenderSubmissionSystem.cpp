#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Core/HIKARI_FrameContext.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};

    const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() {
        return sDebugStats_;
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.fallbackWireCount = 0;

        world.ForEachObjectWith<ModelComponent>([&frame](GameObject& object, ModelComponent& model) {
            if (!model.IsVisible()) {
                return;
            }

            const ModelAsset* asset = model.GetAsset();
            const bool hasLegacyMesh = asset && asset->GetMesh() && asset->GetMesh()->IsValid();
            const bool hasModelPrimitives = asset && !asset->meshes.empty();
            if (asset && asset->GetState() == ModelAsset::State::Loaded && (hasLegacyMesh || hasModelPrimitives)) {
                if (model.GetAnimationAutoPlay() && !model.GetAnimationClip().empty()) {
                    model.SetAnimationTime(model.GetAnimationTime() + frame.gameDt);
                }

                ++sDebugStats_.submittedModelCount;

                ModelRenderItem item{};
                item.model = asset;
                item.worldTransform = object.Transform();
                item.materialFxProfileId = model.GetMaterialFxProfileId();
                item.postGroupMask = model.GetPostGroupMask();
                item.materialFxValuesInitialized = model.AreMaterialFxValuesInitialized();
                for (int i = 0; i < 4; ++i) {
                    item.materialFxParamValues[i] = model.GetMaterialFxParamValues()[i];
                }
                item.animationClipName = model.GetAnimationClip();
                item.animationTimeSec = model.GetAnimationTime();
                item.animationLoop = model.GetAnimationLoop();
                MODELRENDERER::SubmitModel(item);
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
