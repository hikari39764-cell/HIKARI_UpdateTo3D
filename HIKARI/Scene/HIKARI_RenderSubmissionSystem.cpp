#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

namespace HIKARI {

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};

    const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() {
        return sDebugStats_;
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        (void)frame;

        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.fallbackWireCount = 0;
        MESHWIREDEBUG::BeginFrame();

        world.ForEachObjectWith<ModelComponent>([](GameObject& object, ModelComponent& model) {
            if (!model.IsVisible()) {
                return;
            }

            const ModelAsset* asset = nullptr;
            if (model.GetSourceKind() == ModelSourceKind::Procedural) {
                asset = PROCEDURAL::GetOrCreateModel(model.GetProceduralSettings());
            } else {
                asset = model.GetAsset();
            }
            const bool hasLegacyMesh = asset && asset->GetMesh() && asset->GetMesh()->IsValid();
            const bool hasModelPrimitives = asset && !asset->meshes.empty();
            if (asset && asset->GetState() == ModelAsset::State::Loaded && (hasLegacyMesh || hasModelPrimitives)) {
                ++sDebugStats_.submittedModelCount;

                const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
                if (debugMode == ModelRenderDebugMode::BoundsOnly) {
                    MESHWIREDEBUG::SubmitModelBounds(*asset, object.Transform(), model.GetWireColor());
                    return;
                }

                ModelRenderItem item{};
                item.model = asset;
                item.instanceKey = object.GetDocumentId().value;
                if (item.instanceKey == 0) {
                    item.instanceKey = reinterpret_cast<uint64_t>(&object);
                }
                item.worldTransform = object.Transform();
                item.materialFxProfileId = model.GetMaterialFxProfileId();
                item.postGroupMask = model.GetPostGroupMask();
                item.showSkeletonDebug = model.IsSkeletonDebugVisible();
                item.skeletonDebugXRay = model.IsSkeletonDebugXRay();
                item.castShadow = model.GetCastShadow();
                item.receiveShadow = model.GetReceiveShadow();
                if (debugMode == ModelRenderDebugMode::WireOnly) {
                    item.geometryDebugMode = ModelGeometryDebugMode::WireOnly;
                    item.castShadow = false;
                } else if (debugMode == ModelRenderDebugMode::WireOverlay) {
                    item.geometryDebugMode = ModelGeometryDebugMode::WireOverlay;
                }
                item.materialFxValuesInitialized = model.AreMaterialFxValuesInitialized();
                for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                    item.materialFxParamValues[i] = model.GetMaterialFxParamValues()[i];
                }

                if (const AnimatorComponent* animator = object.GetComponent<AnimatorComponent>()) {
                    item.animationClipName = animator->GetClip();
                    item.animationTimeSec = animator->GetTime();
                    item.animationLoop = animator->GetLoop();
                }

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
