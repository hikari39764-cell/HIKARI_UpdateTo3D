#include "Scene/HIKARI_SceneRenderCacheSync.h"

#include <algorithm>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        RENDER3D::RUNTIME::SceneRenderObjectId ResolveSceneRenderObjectId(const GameObject& object) {
            RENDER3D::RUNTIME::SceneRenderObjectId id{ object.GetDocumentId().value };
            if (!id.IsValid()) {
                id.value = reinterpret_cast<uint64_t>(&object);
            }
            return id;
        }

        const ModelAsset* ResolveModelAsset(ModelComponent& model) {
            if (model.GetSourceKind() == ModelSourceKind::Procedural) {
                return PROCEDURAL::GetOrCreateModel(model.GetProceduralSettings());
            }
            return model.GetAsset();
        }

        Bounds ResolveLocalBounds(
            const ModelAsset* model,
            const RENDER3D::RUNTIME::RenderModelAsset* renderModel) {

            if (renderModel != nullptr && renderModel->valid && BOUNDS::IsUsable(renderModel->localBounds)) {
                return renderModel->localBounds;
            }
            if (model != nullptr && BOUNDS::IsUsable(model->bounds)) {
                return model->bounds;
            }
            return {};
        }
    }

    void SceneRenderCacheSync::Sync(
        World& world,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        uint64_t frameIndex) {

        sceneRenderCache.BeginSync(frameIndex);

        world.ForEachObjectWith<ModelComponent>([&](GameObject& object, ModelComponent& model) {
            RENDER3D::RUNTIME::SceneRenderObjectDesc desc{};
            desc.id = ResolveSceneRenderObjectId(object);
            desc.visible = model.IsVisible();

            const ModelAsset* asset = ResolveModelAsset(model);
            desc.model = asset;
            if (asset != nullptr) {
                desc.renderModel = renderModelCache.GetOrCreate(*asset);
            }

            desc.worldTransform = object.Transform();
            desc.localBounds = ResolveLocalBounds(desc.model, desc.renderModel);
            desc.worldBounds = BOUNDS::TransformBounds(desc.localBounds, desc.worldTransform.GetWorldMatrix());

            desc.isStatic = model.IsRenderStatic();
            desc.castShadow = model.GetCastShadow();
            desc.receiveShadow = model.GetReceiveShadow();
            desc.materialOverride = model.GetRuntimeMaterialOverride();
            desc.materialFxProfileId = model.GetMaterialFxProfileId();
            desc.postGroupMask = model.GetPostGroupMask();
            desc.materialFxValuesInitialized = model.AreMaterialFxValuesInitialized();
            for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                desc.materialFxParamValues[i] = model.GetMaterialFxParamValues()[i];
            }

            sceneRenderCache.Upsert(desc);
        });

        sceneRenderCache.EndSync();
        sceneRenderCache.PreRenderSync();
    }

}
