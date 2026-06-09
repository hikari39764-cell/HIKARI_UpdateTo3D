#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_FrameContext.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Cluster/HIKARI_ClusteredCpuPreviewRenderer.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryDebug.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"
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

        bool IsSelectedClusterTarget(
            const RenderSubmissionSystem::ClusteredCpuPreviewTarget& target,
            const GameObject& object) {

            return target.selectedObjectId != 0u &&
                object.GetDocumentId().value == target.selectedObjectId;
        }

        const RENDER3D::CLUSTER::ClusteredGeometryAsset* LoadClusteredGeometryForObject(
            const RenderSubmissionSystem::ClusteredCpuPreviewTarget& target,
            const ModelComponent& model) {

            const std::filesystem::path hcmeshPath = ResolvePreviewHcmeshPath(target, model);
            if (hcmeshPath.empty()) {
                return nullptr;
            }
            return RENDER3D::CLUSTER::GetClusteredGeometryManager().LoadOrGet(hcmeshPath);
        }

        bool TrySubmitSelectedClusterTools(
            const RenderSubmissionSystem::ClusteredCpuPreviewTarget& target,
            const GameObject& object,
            const ModelComponent& model,
            const ModelAsset& asset) {

            const bool wantsPreview =
                target.mode == RENDER3D::CLUSTER::ClusteredRenderMode::SelectedPreview;
            const bool wantsDebug =
                target.debugOptions.mode != RENDER3D::CLUSTER::ClusterDebugViewMode::Off;
            if ((!wantsPreview && !wantsDebug) || !IsSelectedClusterTarget(target, object)) {
                return false;
            }

            const RENDER3D::CLUSTER::ClusteredGeometryAsset* clusteredGeometry =
                LoadClusteredGeometryForObject(target, model);
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

        bool TrySubmitClusteredCpuReference(
            const RenderSubmissionSystem::ClusteredCpuPreviewTarget& target,
            const ModelComponent& model,
            const ModelAsset& asset,
            ModelRenderDebugMode debugMode) {

            if (target.mode != RENDER3D::CLUSTER::ClusteredRenderMode::CpuReference ||
                !model.IsRenderStatic()) {
                return false;
            }

            RENDER3D::CLUSTER::ClusteredCpuPreviewRenderer& referenceRenderer =
                RENDER3D::CLUSTER::GetClusteredCpuPreviewRenderer();
            referenceRenderer.RecordReferenceCandidate();

            if (model.GetSourceKind() != ModelSourceKind::Asset ||
                model.GetAssetId().empty() ||
                debugMode != ModelRenderDebugMode::Normal ||
                asset.HasSkinnedMesh()) {
                referenceRenderer.RecordFallbackObject();
                return false;
            }

            const RENDER3D::CLUSTER::ClusteredGeometryAsset* clusteredGeometry =
                LoadClusteredGeometryForObject(target, model);
            if (clusteredGeometry == nullptr || !clusteredGeometry->valid) {
                referenceRenderer.RecordFallbackObject();
                return false;
            }

            // CPU reference は HCMESH 側の描画が安定するまで旧描画を維持する。
            referenceRenderer.RecordFallbackObject(static_cast<uint32_t>(clusteredGeometry->surfaces.size()));
            return false;
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
    const Camera3D* RenderSubmissionSystem::sActiveRenderCamera_ = nullptr;
    RENDER3D::RUNTIME::SceneRenderCache RenderSubmissionSystem::sSceneRenderCache_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketBuilder RenderSubmissionSystem::sSurfaceDrawPacketBuilder_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketPlanner RenderSubmissionSystem::sSurfaceDrawPacketPlanner_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketPlanOptions RenderSubmissionSystem::sSurfaceDrawPacketPlanOptions_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketPlanStats RenderSubmissionSystem::sSurfaceDrawPacketPlanStats_{};
    SceneRenderCacheSync RenderSubmissionSystem::sSceneRenderCacheSync_{};
    RenderSubmissionSystem::ClusteredCpuPreviewTarget RenderSubmissionSystem::sClusteredCpuPreviewTarget_{};

    void RenderSubmissionSystem::SetActiveRenderCamera(const Camera3D* camera) {
        sActiveRenderCamera_ = camera;
    }

    void RenderSubmissionSystem::SetClusteredCpuPreviewTarget(
        RENDER3D::CLUSTER::ClusteredRenderMode mode,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        uint64_t selectedObjectId,
        RENDER3D::CLUSTER::ClusterDebugOptions debugOptions) {

        sClusteredCpuPreviewTarget_.mode = mode;
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

    const RENDER3D::RUNTIME::SceneRenderCache& RenderSubmissionSystem::GetSceneRenderCache() {
        return sSceneRenderCache_;
    }

    const RENDER3D::RUNTIME::SceneRenderCache::Stats& RenderSubmissionSystem::GetSceneRenderCacheStats() {
        return sSceneRenderCache_.GetStats();
    }

    const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder& RenderSubmissionSystem::GetSurfaceDrawPacketBuilder() {
        return sSurfaceDrawPacketBuilder_;
    }

    const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder::Stats& RenderSubmissionSystem::GetSurfaceDrawPacketStats() {
        return sSurfaceDrawPacketBuilder_.GetStats();
    }

    const RENDER3D::RUNTIME::SurfaceDrawPacketPlanStats& RenderSubmissionSystem::GetSurfaceDrawPacketPlanStats() {
        return sSurfaceDrawPacketPlanStats_;
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.scannedModelCount = 0;
        sDebugStats_.hiddenModelCount = 0;
        sDebugStats_.culledModelCount = 0;
        sDebugStats_.missingBoundsCount = 0;
        sDebugStats_.skinnedCullSkippedCount = 0;
        sDebugStats_.fallbackWireCount = 0;
        sDebugStats_.surfacePacketForwardSkipCount = 0;
        sDebugStats_.surfacePacketShadowSkipCount = 0;
        sDebugStats_.runtimeSpecialModelCount = 0;
        sDebugStats_.runtimeSpecialForwardModelCount = 0;
        sDebugStats_.runtimeSpecialShadowModelCount = 0;
        sDebugStats_.frustumCullingEnabled = sActiveRenderCamera_ != nullptr;

        RENDER3D::CLUSTER::ClusteredCpuPreviewRenderer& clusteredPreview =
            RENDER3D::CLUSTER::GetClusteredCpuPreviewRenderer();
        clusteredPreview.SetMode(sClusteredCpuPreviewTarget_.mode);
        clusteredPreview.ResetFrameStats();

        sSceneRenderCacheSync_.Sync(
            world,
            MODELRENDERER::GetRenderModelCache(),
            sSceneRenderCache_,
            frame.frameIndex);
        // SceneSurfaceInstance から、実行可能な draw packet view を構築する。
        sSurfaceDrawPacketBuilder_.BuildFromSceneRenderCache(sSceneRenderCache_);

        const bool clusteredCpuReferenceActive =
            sClusteredCpuPreviewTarget_.mode == RENDER3D::CLUSTER::ClusteredRenderMode::CpuReference;
        const bool surfacePacketMainRouteActive = true;
        const bool surfacePacketForwardActive =
            surfacePacketMainRouteActive && !clusteredCpuReferenceActive;
        const bool surfacePacketShadowActive = surfacePacketMainRouteActive;
        sDebugStats_.surfacePacketMainRouteActive = surfacePacketMainRouteActive;

        sSurfaceDrawPacketPlanOptions_ = {};
        sSurfaceDrawPacketPlanOptions_.buildForwardPlan = surfacePacketForwardActive;
        sSurfaceDrawPacketPlanOptions_.bypassLegacyForward = surfacePacketForwardActive;
        sSurfaceDrawPacketPlanOptions_.buildShadowPlan = surfacePacketShadowActive;
        sSurfaceDrawPacketPlanOptions_.bypassLegacyShadow = surfacePacketShadowActive;
        sSurfaceDrawPacketPlanOptions_.enableFrustumCulling = true;
        if (sActiveRenderCamera_ != nullptr) {
            // Runtime planner には Camera3D ではなく必要な行列だけを渡す。
            sSurfaceDrawPacketPlanOptions_.cameraView = sActiveRenderCamera_->GetView();
            sSurfaceDrawPacketPlanOptions_.cameraViewProj = sActiveRenderCamera_->GetViewProj();
            sSurfaceDrawPacketPlanOptions_.hasCameraView = true;
            sSurfaceDrawPacketPlanOptions_.hasCameraViewProj = true;
        }
        sSurfaceDrawPacketPlanner_.Build(
            sSurfaceDrawPacketBuilder_,
            sSurfaceDrawPacketPlanOptions_,
            sSurfaceDrawPacketPlanStats_);
        if (surfacePacketForwardActive) {
            MESHRENDERER::SetSurfaceDrawPacketExecutionPlans(
                &sSurfaceDrawPacketBuilder_,
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardOpaquePacketIndices(),
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardOpaqueCommands(),
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardTransparentPacketIndices(),
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardTransparentCommands());
        } else {
            MESHRENDERER::SetSurfaceDrawPacketExecutionPlans(nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        if (surfacePacketShadowActive) {
            SHADOW::SetSurfaceDrawPacketExecutionPlan(
                &sSurfaceDrawPacketBuilder_,
                &sSurfaceDrawPacketPlanner_.GetExecutableShadowPacketIndices(),
                &sSurfaceDrawPacketPlanner_.GetExecutableShadowCommands());
        } else {
            SHADOW::SetSurfaceDrawPacketExecutionPlan(nullptr, nullptr, nullptr);
        }

        MESHWIREDEBUG::BeginFrame();

        world.ForEachObjectWith<ModelComponent>([surfacePacketForwardActive, surfacePacketShadowActive](GameObject& object, ModelComponent& model) {
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
                const RENDER3D::RUNTIME::SceneRenderObjectId renderObjectId =
                    ResolveSceneRenderObjectId(object);
                bool forwardHandledBySurfacePacket = false;
                bool shadowHandledBySurfacePacket = false;

                if (surfacePacketForwardActive) {
                    forwardHandledBySurfacePacket =
                        sSurfaceDrawPacketPlanner_.HasFullForwardCoverageForObject(renderObjectId);
                    if (forwardHandledBySurfacePacket) {
                        ++sDebugStats_.surfacePacketForwardSkipCount;
                        ++sSurfaceDrawPacketPlanStats_.mainForwardBypassObjectCount;
                    }
                }
                if (surfacePacketShadowActive && model.GetCastShadow()) {
                    shadowHandledBySurfacePacket =
                        sSurfaceDrawPacketPlanner_.HasFullShadowCoverageForObject(renderObjectId);
                    if (shadowHandledBySurfacePacket) {
                        ++sDebugStats_.surfacePacketShadowSkipCount;
                        ++sSurfaceDrawPacketPlanStats_.mainShadowBypassObjectCount;
                    }
                }

                if (sActiveRenderCamera_ != nullptr) {
                    if (asset->HasSkinnedMesh()) {
                        // skinning 後の bounds は未確定なので、安全側で描画する。
                        ++sDebugStats_.skinnedCullSkippedCount;
                    } else if (BOUNDS::IsUsable(asset->bounds)) {
                        const MATH::Mat4 localToClip =
                            sActiveRenderCamera_->GetViewProj() * object.Transform().GetWorldMatrix();
                        if (!BOUNDS::IntersectsClipFrustum(asset->bounds, localToClip)) {
                            ++sDebugStats_.culledModelCount;
                            return;
                        }
                    } else {
                        // 古い asset で bounds が無い場合は、安全側で描画する。
                        ++sDebugStats_.missingBoundsCount;
                    }
                }

                const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
                if (debugMode == ModelRenderDebugMode::BoundsOnly) {
                    MESHWIREDEBUG::SubmitModelBounds(*asset, object.Transform(), model.GetWireColor());
                    ++sDebugStats_.submittedModelCount;
                    return;
                }

                const bool clusteredForwardHandled = TrySubmitClusteredCpuReference(
                    sClusteredCpuPreviewTarget_,
                    model,
                    *asset,
                    debugMode);
                TrySubmitSelectedClusterTools(sClusteredCpuPreviewTarget_, object, model, *asset);

                if (forwardHandledBySurfacePacket && (!model.GetCastShadow() || shadowHandledBySurfacePacket)) {
                    return;
                }
                if (clusteredForwardHandled && (!model.GetCastShadow() || shadowHandledBySurfacePacket)) {
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
                if (forwardHandledBySurfacePacket) {
                    item.submitForward = false;
                }
                if (clusteredForwardHandled) {
                    item.submitForward = false;
                }
                if (shadowHandledBySurfacePacket) {
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
