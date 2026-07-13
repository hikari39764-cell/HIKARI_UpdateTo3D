#include "Runtime/HIKARI_RuntimeRenderResourceParking.h"

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/Temporal/HIKARI_TaaResolvePass.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMaskPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RUNTIME {
    namespace {
        bool gParked = false;
        bool gEditorParkedForStandalone = false;
    }

    static bool ParkTransientRenderResources(GFX::Dx12Core& core) {
        if (gParked) {
            return true;
        }
        if (!core.WaitForIdle()) {
            HIKARI_LOG_WARN(
                "Editor preview GPU parking skipped because the GPU did not become idle.");
            return false;
        }

        RENDER3D::UPSCALING::ReleaseStreamlineTransientResources();
        POST::PostSystem::Shutdown();
        RENDER3D::TEMPORAL::ShutdownTaaResolvePass();
        RENDER3D::TEMPORAL::ShutdownTemporalMaskPass();
        RENDER3D::TEMPORAL::ShutdownTemporalGeometryPass();
        RENDER3D::TEMPORAL::ShutdownMotionVectorPass();
        RENDER3D::TEMPORAL::ShutdownTemporalResourceSystem();
        RENDER3D::SCREENSPACE::ReleaseScreenSpaceRuntimeState();
        RENDER3D::VOLUMETRIC::ShutdownVolumetricLightingStage();
        gParked = true;
        HIKARI_LOG_INFO(
            "Editor preview GPU parking released transient render resources.");
        return true;
    }

    static void RestoreTransientRenderResources(const GFX::Context& context) {
        if (!gParked) {
            return;
        }
        POST::PostSystem::Initialize(context);
        RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
            RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
        gParked = false;
        HIKARI_LOG_INFO(
            "Editor preview GPU parking restored transient render resources.");
    }

    bool ParkEditorForStandalone(
        DocumentSceneBase& scene,
        GFX::Dx12Core& core) {
        if (gEditorParkedForStandalone) {
            return true;
        }
        if (!core.WaitForIdle()) {
            HIKARI_LOG_WARN(
                "Standalone parking skipped because the GPU did not become idle.");
            return false;
        }
        if (!scene.ParkRuntimeForStandalone()) {
            return false;
        }

        RENDER3D::ShutdownClusterGeometryResourceSystem();
        if (!ParkTransientRenderResources(core)) {
            RENDER3D::UpdateClusterGeometryResourceContext(core.BuildContext());
            (void)scene.RestoreRuntimeAfterStandalone();
            return false;
        }
        gEditorParkedForStandalone = true;
        HIKARI_LOG_INFO(
            "Editor runtime and GPU scene parked for Standalone Game.");
        return true;
    }

    bool RestoreEditorAfterStandalone(
        DocumentSceneBase& scene,
        const GFX::Context& context) {
        if (!gEditorParkedForStandalone) {
            return true;
        }

        RestoreTransientRenderResources(context);
        RENDER3D::UpdateClusterGeometryResourceContext(context);
        const bool restored = scene.RestoreRuntimeAfterStandalone();
        gEditorParkedForStandalone = false;
        HIKARI_LOG_INFO(
            restored
                ? "Editor runtime restored after Standalone Game."
                : "Editor runtime restore after Standalone Game was incomplete.");
        return restored;
    }

    bool IsEditorParkedForStandalone() {
        return gEditorParkedForStandalone;
    }

} // namespace HIKARI::RUNTIME
