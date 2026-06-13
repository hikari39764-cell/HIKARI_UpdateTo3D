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
#include <vector>

namespace HIKARI {

    namespace {
        RENDER3D::RUNTIME::SceneRenderObjectId ResolveSceneRenderObjectId(const GameObject& object) {
            RENDER3D::RUNTIME::SceneRenderObjectId id{ object.GetDocumentId().value };
            if (!id.IsValid()) {
                id.value = reinterpret_cast<uint64_t>(&object);
            }
            return id;
        }

        struct LegacySurfaceFallbackSubmitStats {
            uint32_t itemCount = 0;
            uint32_t forwardItemCount = 0;
            uint32_t shadowItemCount = 0;
        };

        LegacySurfaceFallbackSubmitStats SubmitLegacySurfaceFallbacks(
            const ModelRenderItem& baseItem,
            RENDER3D::RUNTIME::SceneRenderObjectId objectId,
            const RENDER3D::RUNTIME::SurfaceDrawPacketPlanner& planner,
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawPacket>& packets,
            bool submitForwardFallbacks,
            bool submitShadowFallbacks) {

            LegacySurfaceFallbackSubmitStats stats{};
            if (!objectId.IsValid() ||
                (!submitForwardFallbacks && !submitShadowFallbacks)) {
                return stats;
            }

            for (const RENDER3D::RUNTIME::SurfaceDrawPacket& packet : packets) {
                if (packet.objectId.value != objectId.value ||
                    packet.meshIndex == RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex ||
                    packet.primitiveIndex == RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex) {
                    continue;
                }

                const bool forwardLegacy =
                    submitForwardFallbacks &&
                    packet.forwardCandidate &&
                    !planner.ShouldBypassLegacyForwardSurface(
                        packet.objectId,
                        packet.nodeIndex,
                        packet.meshIndex,
                        packet.primitiveIndex);
                const bool shadowLegacy =
                    submitShadowFallbacks &&
                    packet.shadowCandidate &&
                    !planner.ShouldBypassLegacyShadowSurface(
                        packet.objectId,
                        packet.nodeIndex,
                        packet.meshIndex,
                        packet.primitiveIndex);
                if (!forwardLegacy && !shadowLegacy) {
                    continue;
                }

                ModelRenderItem item = baseItem;
                item.model = packet.model != nullptr ? packet.model : baseItem.model;
                item.materialOverride = packet.materialOverride;
                item.worldTransform = packet.objectWorldTransform;
                item.materialFxProfileId = packet.materialFxProfileId;
                item.postGroupMask = packet.postGroupMask;
                for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                    item.materialFxParamValues[i] = packet.materialFxParamValues[i];
                }
                item.materialFxValuesInitialized = packet.materialFxValuesInitialized;
                item.castShadow = packet.castShadow;
                item.receiveShadow = packet.receiveShadow;
                item.submitForward = forwardLegacy;
                item.submitShadow = shadowLegacy;
                item.useSurfaceFilter = true;
                item.nodeIndexFilter = packet.nodeIndex;
                item.meshIndexFilter = packet.meshIndex;
                item.primitiveIndexFilter = packet.primitiveIndex;

                MODELRENDERER::SubmitModel(item);
                ++stats.itemCount;
                if (forwardLegacy) {
                    ++stats.forwardItemCount;
                }
                if (shadowLegacy) {
                    ++stats.shadowItemCount;
                }
            }

            return stats;
        }
    }

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};
    const Camera3D* RenderSubmissionSystem::sActiveRenderCamera_ = nullptr;
    const AssetRegistry* RenderSubmissionSystem::sAssetRegistry_ = nullptr;
    std::filesystem::path RenderSubmissionSystem::sProjectRoot_{};
    RENDER3D::RUNTIME::SceneRenderCache RenderSubmissionSystem::sSceneRenderCache_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketBuilder RenderSubmissionSystem::sSurfaceDrawPacketBuilder_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketPlanner RenderSubmissionSystem::sSurfaceDrawPacketPlanner_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketPlanOptions RenderSubmissionSystem::sSurfaceDrawPacketPlanOptions_{};
    RENDER3D::RUNTIME::SurfaceDrawPacketPlanStats RenderSubmissionSystem::sSurfaceDrawPacketPlanStats_{};
    SceneRenderCacheSync RenderSubmissionSystem::sSceneRenderCacheSync_{};
    RenderSubmissionRouteMode RenderSubmissionSystem::sRouteMode_ =
        RenderSubmissionRouteMode::SurfacePacketMainline;

    const char* ToString(RenderSubmissionRouteMode mode) {
        switch (mode) {
        case RenderSubmissionRouteMode::SurfacePacketMainline:
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
    }

    void RenderSubmissionSystem::SetRouteMode(RenderSubmissionRouteMode mode) {
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
        sDebugStats_.routeMode = sRouteMode_;
        sDebugStats_.surfacePacketForceLegacyActive =
            sRouteMode_ == RenderSubmissionRouteMode::ForceLegacy;
        sDebugStats_.frustumCullingEnabled = false;

        sSceneRenderCacheSync_.Sync(
            world,
            MODELRENDERER::GetRenderModelCache(),
            sSceneRenderCache_,
            frame.frameIndex,
            sAssetRegistry_,
            sProjectRoot_);
        // SceneSurfaceInstance から、実行可能な draw packet view を構築する。
        sSurfaceDrawPacketBuilder_.BuildFromSceneRenderCache(sSceneRenderCache_);

        const bool surfacePacketMainRouteActive =
            sRouteMode_ != RenderSubmissionRouteMode::ForceLegacy;
        const bool surfacePacketForwardActive = surfacePacketMainRouteActive;
        const bool surfacePacketShadowActive = surfacePacketMainRouteActive;
        const bool bypassLegacyForward = surfacePacketForwardActive;
        const bool bypassLegacyShadow = surfacePacketShadowActive;
        sDebugStats_.surfacePacketMainRouteActive = surfacePacketMainRouteActive;

        sSurfaceDrawPacketPlanOptions_ = {};
        sSurfaceDrawPacketPlanOptions_.buildForwardPlan = surfacePacketForwardActive;
        sSurfaceDrawPacketPlanOptions_.bypassLegacyForward = bypassLegacyForward;
        sSurfaceDrawPacketPlanOptions_.buildShadowPlan = surfacePacketShadowActive;
        sSurfaceDrawPacketPlanOptions_.bypassLegacyShadow = bypassLegacyShadow;
        // GPU 主線では packet 生成時に CPU 側で視錐台 cull しない。
        // SurfaceGpuScene を広く渡し、cluster / meshlet cull が可視性を決める。
        sSurfaceDrawPacketPlanOptions_.enableCpuFrustumCulling =
            !surfacePacketMainRouteActive;
        sDebugStats_.frustumCullingEnabled =
            sActiveRenderCamera_ != nullptr &&
            sSurfaceDrawPacketPlanOptions_.enableCpuFrustumCulling;
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
                &sSurfaceDrawPacketPlanner_.GetForwardOpaqueGpuSceneInstances(),
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardDepthAwarePacketIndices(),
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardDepthAwareCommands(),
                &sSurfaceDrawPacketPlanner_.GetForwardDepthAwareGpuSceneInstances(),
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardTransparentPacketIndices(),
                &sSurfaceDrawPacketPlanner_.GetExecutableForwardTransparentCommands(),
                &sSurfaceDrawPacketPlanner_.GetForwardTransparentGpuSceneInstances());
        } else {
            MESHRENDERER::SetSurfaceDrawPacketExecutionPlans(
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr);
        }
        if (surfacePacketShadowActive) {
            SHADOW::SetSurfaceDrawPacketExecutionPlan(
                &sSurfaceDrawPacketBuilder_,
                &sSurfaceDrawPacketPlanner_.GetExecutableShadowPacketIndices(),
                &sSurfaceDrawPacketPlanner_.GetExecutableShadowCommands(),
                &sSurfaceDrawPacketPlanner_.GetShadowGpuSceneInstances());
        } else {
            SHADOW::SetSurfaceDrawPacketExecutionPlan(nullptr, nullptr, nullptr, nullptr);
        }

        MESHWIREDEBUG::BeginFrame();

        world.ForEachObjectWith<ModelComponent>(
            [bypassLegacyForward, bypassLegacyShadow](
                GameObject& object,
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
                const RENDER3D::RUNTIME::SceneRenderObjectId renderObjectId =
                    ResolveSceneRenderObjectId(object);
                bool forwardHandledBySurfacePacket = false;
                bool shadowHandledBySurfacePacket = false;
                bool forwardHasSurfacePacketCoverage = false;
                bool shadowHasSurfacePacketCoverage = false;

                if (bypassLegacyForward) {
                    forwardHasSurfacePacketCoverage =
                        sSurfaceDrawPacketPlanner_.HasForwardCoverageForObject(renderObjectId);
                    forwardHandledBySurfacePacket =
                        sSurfaceDrawPacketPlanner_.HasFullForwardCoverageForObject(renderObjectId);
                    if (forwardHandledBySurfacePacket) {
                        ++sDebugStats_.surfacePacketForwardSkipCount;
                        ++sSurfaceDrawPacketPlanStats_.mainForwardBypassObjectCount;
                    }
                }
                if (bypassLegacyShadow && model.GetCastShadow()) {
                    shadowHasSurfacePacketCoverage =
                        sSurfaceDrawPacketPlanner_.HasShadowCoverageForObject(renderObjectId);
                    shadowHandledBySurfacePacket =
                        sSurfaceDrawPacketPlanner_.HasFullShadowCoverageForObject(renderObjectId);
                    if (shadowHandledBySurfacePacket) {
                        ++sDebugStats_.surfacePacketShadowSkipCount;
                        ++sSurfaceDrawPacketPlanStats_.mainShadowBypassObjectCount;
                    }
                }

                const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
                const bool fullyHandledBySurfacePacket =
                    forwardHandledBySurfacePacket &&
                    (!model.GetCastShadow() || shadowHandledBySurfacePacket);
                const bool needsLegacyOrDebugCull =
                    !fullyHandledBySurfacePacket ||
                    debugMode != ModelRenderDebugMode::Normal;

                if (needsLegacyOrDebugCull && sActiveRenderCamera_ != nullptr) {
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

                if (debugMode == ModelRenderDebugMode::BoundsOnly) {
                    MESHWIREDEBUG::SubmitModelBounds(*asset, object.Transform(), model.GetWireColor());
                    ++sDebugStats_.submittedModelCount;
                    return;
                }

                if (forwardHandledBySurfacePacket &&
                    (!model.GetCastShadow() || shadowHandledBySurfacePacket)) {
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

                const bool splitForwardFallback =
                    item.submitForward &&
                    bypassLegacyForward &&
                    forwardHasSurfacePacketCoverage &&
                    !forwardHandledBySurfacePacket;
                const bool splitShadowFallback =
                    item.submitShadow &&
                    bypassLegacyShadow &&
                    shadowHasSurfacePacketCoverage &&
                    !shadowHandledBySurfacePacket;
                if (splitForwardFallback || splitShadowFallback) {
                    const LegacySurfaceFallbackSubmitStats fallbackStats =
                        SubmitLegacySurfaceFallbacks(
                            item,
                            renderObjectId,
                            sSurfaceDrawPacketPlanner_,
                            sSurfaceDrawPacketBuilder_.GetPackets(),
                            splitForwardFallback,
                            splitShadowFallback);
                    sDebugStats_.submittedModelCount +=
                        static_cast<int>(fallbackStats.itemCount);
                    sDebugStats_.runtimeSpecialForwardModelCount +=
                        static_cast<int>(fallbackStats.forwardItemCount);
                    sDebugStats_.runtimeSpecialShadowModelCount +=
                        static_cast<int>(fallbackStats.shadowItemCount);

                    if (splitForwardFallback) {
                        item.submitForward = false;
                    }
                    if (splitShadowFallback) {
                        item.submitShadow = false;
                    }
                    if (!item.submitForward && !item.submitShadow) {
                        return;
                    }
                }

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
