#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Core/HIKARI_FrameContext.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

#include <utility>

namespace HIKARI {

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};
    const Camera3D* RenderSubmissionSystem::sActiveRenderCamera_ = nullptr;
    const AssetRegistry* RenderSubmissionSystem::sAssetRegistry_ = nullptr;
    std::filesystem::path RenderSubmissionSystem::sProjectRoot_{};
    RENDER3D::RUNTIME::SceneRenderCache RenderSubmissionSystem::sSceneRenderCache_{};
    RENDER3D::GPUDRIVEN::GpuSceneRegistry RenderSubmissionSystem::sGpuSceneRegistry_{};
    SceneRenderCacheSync RenderSubmissionSystem::sSceneRenderCacheSync_{};
    RenderSubmissionRouteMode RenderSubmissionSystem::sRouteMode_ =
        RenderSubmissionRouteMode::GpuDrivenMainline;
    bool RenderSubmissionSystem::sGpuSceneRegistryValid_ = false;
    bool RenderSubmissionSystem::sGpuDrivenSceneSyncInitialized_ = false;

    const char* ToString(RenderSubmissionRouteMode mode) {
        switch (mode) {
        case RenderSubmissionRouteMode::GpuDrivenMainline:
            return "GPU Driven Mainline";
        case RenderSubmissionRouteMode::ForceLegacy:
            return "Force Legacy";
        default:
            return "Unknown";
        }
    }

    void RenderSubmissionSystem::SetActiveRenderCamera(const Camera3D* camera) {
        sActiveRenderCamera_ = camera;
    }

    void RenderSubmissionSystem::SetAssetContext(
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot) {

        sAssetRegistry_ = assetRegistry;
        sProjectRoot_ = std::move(projectRoot);
        sGpuDrivenSceneSyncInitialized_ = false;
    }

    void RenderSubmissionSystem::SetRouteMode(RenderSubmissionRouteMode mode) {
        if (sRouteMode_ != mode) {
            sGpuDrivenSceneSyncInitialized_ = false;
            if (mode == RenderSubmissionRouteMode::ForceLegacy) {
                sGpuSceneRegistry_.Clear();
                sGpuSceneRegistryValid_ = false;
            }
        }
        sRouteMode_ = mode;
    }

    RenderSubmissionRouteMode RenderSubmissionSystem::GetRouteMode() {
        return sRouteMode_;
    }

    const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() {
        return sDebugStats_;
    }

    const RENDER3D::RUNTIME::SceneRenderCache& RenderSubmissionSystem::GetSceneRenderCache() {
        return sSceneRenderCache_;
    }

    const RENDER3D::RUNTIME::SceneRenderCache::Stats& RenderSubmissionSystem::GetSceneRenderCacheStats() {
        return sSceneRenderCache_.GetStats();
    }

    const RENDER3D::GPUDRIVEN::GpuSceneRegistryStats& RenderSubmissionSystem::GetGpuSceneRegistryStats() {
        return sGpuSceneRegistry_.GetStats();
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.scannedModelCount = 0;
        sDebugStats_.hiddenModelCount = 0;
        sDebugStats_.culledModelCount = 0;
        sDebugStats_.missingBoundsCount = 0;
        sDebugStats_.skinnedCullSkippedCount = 0;
        sDebugStats_.fallbackWireCount = 0;
        sDebugStats_.gpuDrivenForwardBypassCount = 0;
        sDebugStats_.gpuDrivenShadowBypassCount = 0;
        sDebugStats_.runtimeSpecialModelCount = 0;
        sDebugStats_.runtimeSpecialForwardModelCount = 0;
        sDebugStats_.runtimeSpecialShadowModelCount = 0;
        sDebugStats_.routeMode = sRouteMode_;
        sDebugStats_.forceLegacyActive =
            sRouteMode_ == RenderSubmissionRouteMode::ForceLegacy;
        sDebugStats_.frustumCullingEnabled = false;

        const bool gpuDrivenMainRouteActive =
            sRouteMode_ != RenderSubmissionRouteMode::ForceLegacy;
        const bool gpuDrivenForwardActive = gpuDrivenMainRouteActive;
        sDebugStats_.gpuDrivenMainRouteActive = gpuDrivenMainRouteActive;

        if (gpuDrivenMainRouteActive) {
            if (!sGpuDrivenSceneSyncInitialized_) {
                sSceneRenderCacheSync_.Sync(
                    world,
                    MODELRENDERER::GetRenderModelCache(),
                    sSceneRenderCache_,
                    frame.frameIndex,
                    sAssetRegistry_,
                    sProjectRoot_);
                sGpuDrivenSceneSyncInitialized_ = true;
            } else {
                sSceneRenderCacheSync_.SyncDirty(
                    world,
                    MODELRENDERER::GetRenderModelCache(),
                    sSceneRenderCache_,
                    frame.frameIndex,
                    sAssetRegistry_,
                    sProjectRoot_);
            }
        } else {
            sSceneRenderCacheSync_.Sync(
                world,
                MODELRENDERER::GetRenderModelCache(),
                sSceneRenderCache_,
                frame.frameIndex,
                sAssetRegistry_,
                sProjectRoot_);
        }

        sDebugStats_.frustumCullingEnabled =
            sActiveRenderCamera_ != nullptr &&
            !gpuDrivenMainRouteActive;
        if (gpuDrivenMainRouteActive) {
            RENDER3D::GPUDRIVEN::GpuSceneRegistrySyncInput gpuSceneInput{};
            gpuSceneInput.sceneCache = &sSceneRenderCache_;
            sGpuSceneRegistry_.SyncForwardFromSceneCache(gpuSceneInput);

            sGpuSceneRegistryValid_ = true;
        } else {
            sGpuSceneRegistry_.Clear();
            sGpuSceneRegistryValid_ = false;
        }

        if (gpuDrivenForwardActive && sGpuSceneRegistryValid_) {
            MESHRENDERER::SetGpuDrivenSceneSource(
                &sGpuSceneRegistry_.GetSceneSource());
        } else {
            MESHRENDERER::SetGpuDrivenSceneSource(nullptr);
        }
        if (gpuDrivenMainRouteActive &&
            sGpuSceneRegistryValid_ &&
            sGpuSceneRegistry_.HasShadowPassSource()) {

            SHADOW::SetGpuDrivenSceneSource(
                &sGpuSceneRegistry_.GetSceneSource());
        } else {
            SHADOW::SetGpuDrivenSceneSource(nullptr);
        }

        MESHWIREDEBUG::BeginFrame();

        if (gpuDrivenMainRouteActive) {
            return;
        }

        world.ForEachObjectWith<ModelComponent>(
            [](GameObject& object,
                ModelComponent& model) {
            ++sDebugStats_.scannedModelCount;
            if (!model.IsVisible()) {
                ++sDebugStats_.hiddenModelCount;
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
                const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
                if (sActiveRenderCamera_ != nullptr) {
                    if (asset->HasSkinnedMesh()) {
                        ++sDebugStats_.skinnedCullSkippedCount;
                    } else if (BOUNDS::IsUsable(asset->bounds)) {
                        const MATH::Mat4 localToClip =
                            sActiveRenderCamera_->GetViewProj() * object.Transform().GetWorldMatrix();
                        if (!BOUNDS::IntersectsClipFrustum(asset->bounds, localToClip)) {
                            ++sDebugStats_.culledModelCount;
                            return;
                        }
                    } else {
                        ++sDebugStats_.missingBoundsCount;
                    }
                }

                if (debugMode == ModelRenderDebugMode::BoundsOnly) {
                    MESHWIREDEBUG::SubmitModelBounds(*asset, object.Transform(), model.GetWireColor());
                    ++sDebugStats_.submittedModelCount;
                    return;
                }

                ModelRenderItem item{};
                item.model = asset;
                item.materialOverride = model.GetRuntimeMaterialOverride();
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

                if (!item.submitForward && !item.submitShadow) {
                    return;
                }

                ++sDebugStats_.runtimeSpecialModelCount;

                if (item.submitForward) {
                    ++sDebugStats_.runtimeSpecialForwardModelCount;
                }
                if (item.submitShadow) {
                    ++sDebugStats_.runtimeSpecialShadowModelCount;
                }
                MODELRENDERER::SubmitModel(item);
                ++sDebugStats_.submittedModelCount;
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
