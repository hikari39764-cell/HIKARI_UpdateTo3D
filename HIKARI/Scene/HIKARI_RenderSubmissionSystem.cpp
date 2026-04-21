#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};

    const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() {
        return sDebugStats_;
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        (void)frame;

        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.fallbackWireCount = 0;

        world.ForEachObjectWith<ModelComponent>([](GameObject& object, ModelComponent& model) {
            if (!model.IsVisible()) {
                return;
            }

            const ModelAsset* asset = model.GetAsset();
            if (asset && asset->GetState() == ModelAsset::State::Loaded && asset->GetMesh() && asset->GetMesh()->IsValid()) {
                ++sDebugStats_.submittedModelCount;

                MESHRENDERER::SubmitStaticMesh(
                    *asset,
                    object.Transform(),
                    model.GetMaterialFxProfileId(),
                    model.GetPostGroupMask(),
                    model.GetMaterialFxParamValues(),
                    model.AreMaterialFxValuesInitialized()
                );
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