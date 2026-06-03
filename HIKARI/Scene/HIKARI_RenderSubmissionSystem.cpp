#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_FrameContext.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Cluster/HIKARI_ClusteredCpuPreviewRenderer.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryDebug.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

#include <utility>

namespace HIKARI {

    namespace {
        std::filesystem::path ResolvePreviewHcmeshPath(
            const RenderSubmissionSystem::ClusteredCpuPreviewTarget& target,
            const ModelComponent& model) {

            if (!target.assetRegistry || model.GetAssetId().empty()) {
                return {};
            }

            const auto* descriptor =
                target.assetRegistry->FindAs<ModelAssetDescriptor>(AssetId{ model.GetAssetId() });
            if (descriptor == nullptr || descriptor->clusteredGeometryPath.empty()) {
                return {};
            }

            std::filesystem::path path = descriptor->clusteredGeometryPath;
            if (!path.is_absolute() && !target.projectRoot.empty()) {
                path = (target.projectRoot / path).lexically_normal();
            }
            return path;
        }

        bool TrySubmitClusteredTools(
            const RenderSubmissionSystem::ClusteredCpuPreviewTarget& target,
            const GameObject& object,
            const ModelComponent& model,
            const ModelAsset& asset) {

            const bool wantsPreview = target.enabled;
            const bool wantsDebug =
                target.debugOptions.mode != RENDER3D::CLUSTER::ClusterDebugViewMode::Off;
            if ((!wantsPreview && !wantsDebug) ||
                target.selectedObjectId == 0u ||
                object.GetDocumentId().value != target.selectedObjectId) {
                return false;
            }

            const std::filesystem::path hcmeshPath = ResolvePreviewHcmeshPath(target, model);
            if (hcmeshPath.empty()) {
                return false;
            }

            const RENDER3D::CLUSTER::ClusteredGeometryAsset* clusteredGeometry =
                RENDER3D::CLUSTER::GetClusteredGeometryManager().LoadOrGet(hcmeshPath);
            if (clusteredGeometry == nullptr) {
                return false;
            }

            if (wantsDebug) {
                RENDER3D::CLUSTER::SubmitClusterDebugOverlay(
                    *clusteredGeometry,
                    object.Transform(),
                    target.debugOptions);
            }
            if (!wantsPreview) {
                return true;
            }

            return RENDER3D::CLUSTER::GetClusteredCpuPreviewRenderer().SubmitSelectedObjectPreview(
                *clusteredGeometry,
                object.Transform(),
                &asset,
                model.GetReceiveShadow(),
                MESHRENDERER::MeshRenderDebugMode::WireOverlay,
                model.GetRuntimeMaterialOverride());
        }
        RENDER3D::RUNTIME::SceneRenderObjectId ResolveSceneRenderObjectId(const GameObject& object) {
            RENDER3D::RUNTIME::SceneRenderObjectId id{ object.GetDocumentId().value };
            if (!id.IsValid()) {
                id.value = reinterpret_cast<uint64_t>(&object);
            }
            return id;
        }
    }

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};
    RenderSubmissionOptions RenderSubmissionSystem::sOptions_{};
    const Camera3D* RenderSubmissionSystem::sActiveRenderCamera_ = nullptr;
    RENDER3D::RUNTIME::SceneRenderCache RenderSubmissionSystem::sSceneRenderCache_{};
    RENDER3D::RUNTIME::StaticDrawRecordCache RenderSubmissionSystem::sStaticDrawRecordCache_{};
    RENDER3D::RUNTIME::StaticRecordSubmitOptions RenderSubmissionSystem::sStaticRecordSubmitOptions_{};
    RENDER3D::RUNTIME::StaticDrawRecordSubmitter RenderSubmissionSystem::sStaticDrawRecordSubmitter_{};
    RENDER3D::RUNTIME::StaticDrawRecordSubmitStats RenderSubmissionSystem::sStaticDrawRecordSubmitStats_{};
    SceneRenderCacheSync RenderSubmissionSystem::sSceneRenderCacheSync_{};
    RenderSubmissionSystem::ClusteredCpuPreviewTarget RenderSubmissionSystem::sClusteredCpuPreviewTarget_{};

    void RenderSubmissionSystem::SetActiveRenderCamera(const Camera3D* camera) {
        sActiveRenderCamera_ = camera;
    }

    void RenderSubmissionSystem::SetClusteredCpuPreviewTarget(
        bool enabled,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        uint64_t selectedObjectId,
        RENDER3D::CLUSTER::ClusterDebugOptions debugOptions) {

        sClusteredCpuPreviewTarget_.enabled = enabled;
        sClusteredCpuPreviewTarget_.assetRegistry = assetRegistry;
        sClusteredCpuPreviewTarget_.projectRoot = std::move(projectRoot);
        sClusteredCpuPreviewTarget_.selectedObjectId = selectedObjectId;
        sClusteredCpuPreviewTarget_.debugOptions = debugOptions;
    }

    void RenderSubmissionSystem::ClearClusteredCpuPreviewTarget() {
        sClusteredCpuPreviewTarget_ = {};
    }

    const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() {
        return sDebugStats_;
    }

    void RenderSubmissionSystem::SetUseStaticDrawRecordCache(bool enabled) {
        sOptions_.useStaticDrawRecordCache = enabled;
    }

    bool RenderSubmissionSystem::IsUseStaticDrawRecordCacheEnabled() {
        return sOptions_.useStaticDrawRecordCache;
    }

    void RenderSubmissionSystem::SetUseCachedStaticForward(bool enabled) {
        sOptions_.useCachedStaticForward = enabled;
    }

    bool RenderSubmissionSystem::IsUseCachedStaticForwardEnabled() {
        return sOptions_.useCachedStaticForward;
    }

    void RenderSubmissionSystem::SetUseCachedStaticShadow(bool enabled) {
        sOptions_.useCachedStaticShadow = enabled;
    }

    bool RenderSubmissionSystem::IsUseCachedStaticShadowEnabled() {
        return sOptions_.useCachedStaticShadow;
    }

    void RenderSubmissionSystem::SetBypassOldStaticModelRenderer(bool enabled) {
        sOptions_.bypassOldStaticModelRendererWhenFullyCached = enabled;
    }

    bool RenderSubmissionSystem::IsBypassOldStaticModelRendererEnabled() {
        return sOptions_.bypassOldStaticModelRendererWhenFullyCached;
    }

    const RENDER3D::RUNTIME::SceneRenderCache& RenderSubmissionSystem::GetSceneRenderCache() {
        return sSceneRenderCache_;
    }

    const RENDER3D::RUNTIME::SceneRenderCache::Stats& RenderSubmissionSystem::GetSceneRenderCacheStats() {
        return sSceneRenderCache_.GetStats();
    }

    const RENDER3D::RUNTIME::StaticDrawRecordCache& RenderSubmissionSystem::GetStaticDrawRecordCache() {
        return sStaticDrawRecordCache_;
    }

    const RENDER3D::RUNTIME::StaticDrawRecordCache::Stats& RenderSubmissionSystem::GetStaticDrawRecordCacheStats() {
        return sStaticDrawRecordCache_.GetStats();
    }

    const RENDER3D::RUNTIME::StaticDrawRecordSubmitStats& RenderSubmissionSystem::GetStaticDrawRecordSubmitStats() {
        return sStaticDrawRecordSubmitStats_;
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.scannedModelCount = 0;
        sDebugStats_.hiddenModelCount = 0;
        sDebugStats_.culledModelCount = 0;
        sDebugStats_.missingBoundsCount = 0;
        sDebugStats_.skinnedCullSkippedCount = 0;
        sDebugStats_.fallbackWireCount = 0;
        sDebugStats_.staticCachedForwardSkipCount = 0;
        sDebugStats_.staticCachedShadowSkipCount = 0;
        sDebugStats_.staticCachedBypassOldModelRendererCount = 0;
        sDebugStats_.staticCachedFallbackCount = 0;
        sDebugStats_.staticCachedCandidateCount = 0;
        sDebugStats_.staticCachedCulledRecordCount = 0;
        sDebugStats_.staticCachedSubmittedRecordCount = 0;
        sDebugStats_.staticCachedSubmittedForwardRecordCount = 0;
        sDebugStats_.staticCachedSubmittedShadowRecordCount = 0;
        sDebugStats_.frustumCullingEnabled = sActiveRenderCamera_ != nullptr;

        RENDER3D::CLUSTER::ClusteredCpuPreviewRenderer& clusteredPreview =
            RENDER3D::CLUSTER::GetClusteredCpuPreviewRenderer();
        clusteredPreview.SetEnabled(sClusteredCpuPreviewTarget_.enabled);
        clusteredPreview.ResetFrameStats();

        sSceneRenderCacheSync_.Sync(
            world,
            MODELRENDERER::GetRenderModelCache(),
            sSceneRenderCache_,
            frame.frameIndex);
        // 旧描画経路を変えず、静的 draw record だけを同期する。
        sStaticDrawRecordCache_.SyncFromSceneRenderCache(sSceneRenderCache_);

        sStaticRecordSubmitOptions_ = {};
        if (sOptions_.useStaticDrawRecordCache) {
            sStaticRecordSubmitOptions_.useCachedStaticForward = sOptions_.useCachedStaticForward;
            sStaticRecordSubmitOptions_.skipOldStaticForwardSubmit = sOptions_.skipOldStaticForwardWhenCached;
            sStaticRecordSubmitOptions_.useCachedStaticShadow = sOptions_.useCachedStaticShadow;
            sStaticRecordSubmitOptions_.skipOldStaticShadowSubmit = sOptions_.skipOldStaticShadowWhenCached;
            sStaticRecordSubmitOptions_.enableFrustumCulling = true;
            if (sActiveRenderCamera_ != nullptr) {
                // Runtime 層へ Camera3D を渡さず、必要な行列だけを渡す。
                sStaticRecordSubmitOptions_.cameraViewProj = sActiveRenderCamera_->GetViewProj();
                sStaticRecordSubmitOptions_.hasCameraViewProj = true;
            }
        }

        sStaticDrawRecordSubmitter_.Submit(
            sStaticDrawRecordCache_,
            sStaticRecordSubmitOptions_,
            sStaticDrawRecordSubmitStats_);
        sDebugStats_.staticCachedCulledRecordCount =
            static_cast<int>(sStaticDrawRecordSubmitStats_.culledRecordCount);
        sDebugStats_.staticCachedSubmittedRecordCount =
            static_cast<int>(sStaticDrawRecordSubmitStats_.submittedForwardRecordCount);
        sDebugStats_.staticCachedSubmittedForwardRecordCount =
            static_cast<int>(sStaticDrawRecordSubmitStats_.submittedForwardRecordCount);
        sDebugStats_.staticCachedSubmittedShadowRecordCount =
            static_cast<int>(sStaticDrawRecordSubmitStats_.submittedShadowRecordCount);

        MESHWIREDEBUG::BeginFrame();

        world.ForEachObjectWith<ModelComponent>([](GameObject& object, ModelComponent& model) {
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
                const bool isStaticModel = model.IsRenderStatic();
                const bool useCachedStaticPath = sOptions_.useStaticDrawRecordCache && isStaticModel;
                bool canUseCachedForward = false;
                bool canUseCachedShadow = false;
                bool forwardHandledByCache = false;
                bool shadowHandledByCacheOrNotNeeded = !model.GetCastShadow();

                if (useCachedStaticPath) {
                    ++sDebugStats_.staticCachedCandidateCount;
                    const RENDER3D::RUNTIME::SceneRenderObjectId renderObjectId =
                        ResolveSceneRenderObjectId(object);
                    const bool fullCoverage =
                        sStaticDrawRecordCache_.HasFullForwardCoverageForObject(renderObjectId);
                    canUseCachedForward =
                        sOptions_.useCachedStaticForward &&
                        fullCoverage;
                    canUseCachedShadow =
                        sOptions_.useCachedStaticShadow &&
                        fullCoverage &&
                        model.GetCastShadow();

                    forwardHandledByCache =
                        canUseCachedForward &&
                        sOptions_.skipOldStaticForwardWhenCached;
                    shadowHandledByCacheOrNotNeeded =
                        !model.GetCastShadow() ||
                        (canUseCachedShadow && sOptions_.skipOldStaticShadowWhenCached);

                    if (forwardHandledByCache) {
                        ++sDebugStats_.staticCachedForwardSkipCount;
                    }
                    if (canUseCachedShadow && sOptions_.skipOldStaticShadowWhenCached) {
                        ++sDebugStats_.staticCachedShadowSkipCount;
                    }
                    if (!fullCoverage) {
                        ++sDebugStats_.staticCachedFallbackCount;
                    }
                }

                if (sActiveRenderCamera_ != nullptr) {
                    if (asset->HasSkinnedMesh()) {
                        // skinning 後の bounds は未確定なので安全側で描画する。
                        ++sDebugStats_.skinnedCullSkippedCount;
                    } else if (BOUNDS::IsUsable(asset->bounds)) {
                        const MATH::Mat4 localToClip =
                            sActiveRenderCamera_->GetViewProj() * object.Transform().GetWorldMatrix();
                        if (!BOUNDS::IntersectsClipFrustum(asset->bounds, localToClip)) {
                            ++sDebugStats_.culledModelCount;
                            return;
                        }
                    } else {
                        // 古い asset は bounds が無い場合があるため、安全側で描画する。
                        ++sDebugStats_.missingBoundsCount;
                    }
                }

                const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
                if (debugMode == ModelRenderDebugMode::BoundsOnly) {
                    MESHWIREDEBUG::SubmitModelBounds(*asset, object.Transform(), model.GetWireColor());
                    ++sDebugStats_.submittedModelCount;
                    return;
                }

                if (forwardHandledByCache &&
                    shadowHandledByCacheOrNotNeeded &&
                    sOptions_.bypassOldStaticModelRendererWhenFullyCached) {
                    TrySubmitClusteredTools(sClusteredCpuPreviewTarget_, object, model, *asset);
                    ++sDebugStats_.staticCachedBypassOldModelRendererCount;
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
                if (forwardHandledByCache) {
                    item.submitForward = false;
                }
                if (canUseCachedShadow && sOptions_.skipOldStaticShadowWhenCached) {
                    item.submitShadow = false;
                }
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
                TrySubmitClusteredTools(sClusteredCpuPreviewTarget_, object, model, *asset);
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
