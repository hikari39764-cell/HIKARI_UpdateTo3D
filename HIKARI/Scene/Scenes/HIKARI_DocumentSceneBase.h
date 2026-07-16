#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Sequence/HIKARI_SequenceAssetStore.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Gizmos/HIKARI_LightProbeVolumeGizmoRenderer.h"
#include "Editor/Gizmos/HIKARI_ReflectionProbeGizmoRenderer.h"
#endif
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_CinematicCameraPlayback.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI {

    struct PbrMaterialAssetData;
    struct ReflectionProbeBakeJob;
    struct LightProbeBakeJob;

    class DocumentSceneBase : public IScene {
    public:
        explicit DocumentSceneBase(std::string sceneId);
        ~DocumentSceneBase() override;

        void OnEnter() override;
        void OnExit() override;
        void Update(float dt) override;
        void Render() override;
        void RenderImGui() override;

        const std::string& GetSceneId() const override;
        const std::string& GetScenePath() const;
        uint64_t GetSceneDocumentRevision() const noexcept;
        void SetSceneId(std::string sceneId);
        void SetScenePath(std::string scenePath);

        World& GetWorld();
        const World& GetWorld() const;

        SceneDocument& GetSceneDocument();
        const SceneDocument& GetSceneDocument() const;

        SceneEnvironment& GetSceneEnvironment();
        const SceneEnvironment& GetSceneEnvironment() const;
        AssetDatabase& GetAssetDatabase();
        const AssetDatabase& GetAssetDatabase() const;
        AssetRegistry& GetAssetRegistry();
        const AssetRegistry& GetAssetRegistry() const;
        ModelManager& GetModelManager();
        SkyManager& GetSkyManager();
        ComponentRegistry& GetComponentRegistry();
        SceneRuntimeBuilder& GetRuntimeBuilder();
        const SceneRuntimeBuilder& GetRuntimeBuilder() const;

        Camera3D& GetCamera();
        const Camera3D& GetCamera() const;
        CameraDirector& GetCameraDirector();
        const CameraDirector& GetCameraDirector() const;
        const RENDER3D::ResolvedCameraFrame& GetResolvedCameraFrame() const;
        DebugCameraController3D& GetDebugCamera();

        bool& GetEnvironmentLightingEnabled();
        bool ReloadAssets();
        bool ReloadSceneDocument();
        bool RebuildRuntimeWorld();
        bool ApplySystemRuntimeChanges();
        bool ApplyCameraRuntimeChanges();
        bool SetGameDefaultCamera(SceneObjectId cameraObjectId);
        void ClearGameDefaultCamera();
        bool BeginEditorCameraPreview(
            SceneObjectId cameraObjectId,
            const CameraBlendDesc& blend = {},
            bool forceRestart = false);
        bool UpdateEditorCameraPreview(
            const CinematicCameraEvaluation& evaluation);
        void EndEditorCameraPreview();
        bool IsEditorCameraPreviewActive() const;
        SceneObjectId GetEditorCameraPreviewObjectId() const;
        bool TryResolveCameraObjectView(
            SceneObjectId cameraObjectId,
            float aspect,
            Camera3D& outCamera) const;
        bool ApplyCameraObjectPose(
            SceneObjectId cameraObjectId,
            const MATH::Vec3& position,
            const MATH::Quat& rotation,
            bool markDirty = true);
        bool SnapCameraObjectToEditorView(SceneObjectId cameraObjectId);
        CinematicPlaybackHandle PlayCameraSequence(
            CinematicSequenceId sequenceId,
            const CinematicPlaybackOptions& options = {},
            float startTimeSeconds = 0.0f);
        bool StopCameraSequence(CinematicPlaybackHandle handle);
        bool PauseCameraSequence(CinematicPlaybackHandle handle);
        bool ResumeCameraSequence(CinematicPlaybackHandle handle);
        bool SeekCameraSequence(
            CinematicPlaybackHandle handle,
            float timeSeconds);
        bool IsCameraSequencePlaying() const noexcept;
        CinematicPlaybackHandle GetCameraSequencePlaybackHandle() const noexcept;
        SEQUENCER::SequencePlaybackHandle PlaySequence(
            const SEQUENCER::SequencePlayRequest& request);
        SEQUENCER::SequencePlayResult PlaySequenceDetailed(
            const SEQUENCER::SequencePlayRequest& request);
        bool StopSequence(SEQUENCER::SequencePlaybackHandle handle);
        bool PauseSequence(SEQUENCER::SequencePlaybackHandle handle);
        bool ResumeSequence(SEQUENCER::SequencePlaybackHandle handle);
        bool SeekSequence(
            SEQUENCER::SequencePlaybackHandle handle,
            float timeSeconds);
        uint64_t SubmitSequenceCommand(
            SEQUENCER::SequencePlaybackCommand command);
        bool IsSequencePlaying(
            SEQUENCER::SequencePlaybackHandle handle) const noexcept;
        bool IsSequenceActive(
            SEQUENCER::SequencePlaybackHandle handle) const noexcept;
        bool TryGetSequencePlaybackSnapshot(
            SEQUENCER::SequencePlaybackHandle handle,
            SEQUENCER::SequencePlaybackSnapshot& outSnapshot)
            const noexcept;
        std::vector<SEQUENCER::SequencePlaybackEvent>
            ConsumeSequencePlaybackEvents();
        bool BeginRuntimePlay();
        bool EndRuntimePlay();
        bool IsRuntimePlayActive() const { return runtimePlayActive_; }
        bool ParkRuntimeForStandalone();
        bool RestoreRuntimeAfterStandalone();
        bool IsRuntimeParkedForStandalone() const { return runtimeParkedForStandalone_; }
        bool RequestOpenSceneAsset(const AssetGuid& sceneGuid);
        bool OpenSceneAssetNow(const AssetGuid& sceneGuid);
        bool OpenStartupSceneAsset();
        bool CreateTransientEmptySceneDocument();
        bool HasUnsavedSceneChanges() const;
        void SetUnsavedSceneChanges(bool dirty);
        bool ApplyEnvironmentRuntimeChanges();
        bool RefreshLightingRuntime();
        bool RefreshSkyRuntime();
        bool RefreshCurrentSkyRuntime();
        bool RefreshTextureRuntimeByPath(const std::string& path);
        bool ReloadModelAssetRuntime(const AssetId& modelId);
        int RebindModelComponents();
        int RebuildMaterialOverrides();
        int RebuildMaterialOverridesForMaterial(const AssetGuid& materialGuid);
        int ApplyRuntimeMaterialOverridePreview(
            const AssetGuid& materialGuid,
            const PbrMaterialAssetData& data);
        bool RequestReflectionProbeBake();
        bool RequestLightProbeBake();
        TOOLS::BAKING::LightingBakeJobState GetLightingBakeJobState() const;
        bool HasLastLightingBakeReport() const;
        const TOOLS::BAKING::LightingBakeReport& GetLastLightingBakeReport() const;
        bool SaveCurrentSceneDocument();
        bool SaveCurrentSceneDocumentAs(const AssetGuid& sceneGuid);
        const AssetGuid& GetCurrentSceneAssetGuid() const;
        bool IsCurrentSceneAsset(const AssetGuid& guid) const;
        std::string GetCurrentSceneDisplayName() const;

        void SetComponentGizmoState(const ComponentGizmoState& state);
        void SetViewportOverlayState(const ViewportOverlayState& state);
        void SetViewportPerformanceState(const ViewportPerformanceState& state);
        void SetViewportDebugViewState(const ViewportDebugViewState& state);
        void SetSelectedGizmoObjectId(SceneObjectId id);
        void SyncReflectionProbeRuntimeFromAuthoring();

    protected:
        void RegisterDefaultComponentTypes();
        void RegisterDefaultSystemTypes();
        bool BuildSystemScheduleFromDocument();

        virtual bool UseDebugCamera() const;
        virtual bool DrawDebugHelpers() const;
        virtual bool UseEnvironmentLighting() const;
        bool HasRuntimeSceneCameraDriver() const;
        void ConfigureModelTextureResolver();
        bool ProcessReflectionProbeBakeJob();
        bool ProcessLightProbeBakeJob();
        bool RenderSceneForReflectionProbeCaptureFace(
            const Camera3D& faceCamera,
            const SceneEnvironment& captureEnvironment,
            uint32_t faceIndex);
        bool RenderSceneForLightProbeCaptureFace(
            const Camera3D& faceCamera,
            const SceneEnvironment& captureEnvironment,
            uint32_t faceIndex);
        std::string ResolveModelTexturePathFromAssets(
            const std::string& sourceTexturePath,
            ModelTextureUsage usage) const;
        const AssetRecord* FindUniqueTextureAssetByFilename(
            const std::string& filename,
            const std::string& sourceTexturePath) const;

    protected:
        std::string sceneId_;
        std::string scenePath_{};

        Camera3D camera_{};
        Camera3D gameplayCamera_{};
        CameraDirector cameraDirector_{};
        CinematicPlaybackHandle currentCameraSequenceHandle_{};
        RENDER3D::ResolvedCameraFrame resolvedCameraFrame_{};
        DebugCameraController3D debugCamera_{};
        DebugCameraController3D runtimePreviewCamera_{};
        Camera3D editorCameraSnapshot_{};
        Camera3D editorCameraPreviewSnapshot_{};
        DebugCameraController3D editorDebugCameraSnapshot_{};
        ComponentGizmoState editorComponentGizmoSnapshot_{};
        ViewportOverlayState editorViewportOverlaySnapshot_{};
        ViewportPerformanceState editorViewportPerformanceSnapshot_{};
        ViewportDebugViewState editorViewportDebugViewSnapshot_{};
        SceneObjectId editorSelectedGizmoObjectSnapshot_{};
        SceneObjectId editorCameraPreviewObjectId_{};
        CameraOverrideToken editorCameraPreviewToken_{};
        bool editorCameraCutPending_ = false;
        bool runtimePreviewCameraActive_ = false;
        bool runtimeSceneCameraActive_ = false;
        bool runtimePlayActive_ = false;
        bool runtimeParkedForStandalone_ = false;
        World world_{};
        SystemTypeRegistry systemTypeRegistry_{};
        SystemScheduler systemScheduler_{};
        ModelManager modelManager_{};
        SkyManager skyManager_{};
        SceneEnvironment environment_{};
        bool environmentLightingEnabled_ = true;

        AssetDatabase assetDatabase_{};
        AssetRegistry assetRegistry_{};
        SequenceAssetStore sequenceAssetStore_{};
        SEQUENCER::SequencePlaybackService sequencePlaybackService_{};
        ComponentRegistry componentRegistry_{};
        SceneSerializer sceneSerializer_{};
        SceneRuntimeBuilder runtimeBuilder_{};
        SceneDocument sceneDocument_{};
        uint64_t sceneDocumentRevision_ = 0;
        AssetGuid currentSceneAssetGuid_{};
        bool sceneDocumentDirty_ = false;
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::ReflectionProbeGizmoRenderer reflectionProbeGizmoRenderer_{};
        EDITOR::LightProbeVolumeGizmoRenderer lightProbeVolumeGizmoRenderer_{};
#endif
        ComponentGizmoRenderer componentGizmoRenderer_{};
        ComponentGizmoState componentGizmoState_{};
        ViewportOverlayState viewportOverlayState_{};
        ViewportPerformanceState viewportPerformanceState_{};
        ViewportDebugViewState viewportDebugViewState_{};
        SceneObjectId selectedGizmoObjectId_{};
        std::unique_ptr<ReflectionProbeBakeJob> reflectionProbeBakeJob_{};
        std::unique_ptr<LightProbeBakeJob> lightProbeBakeJob_{};
        TOOLS::BAKING::LightingBakeReport lastLightingBakeReport_{};
        bool hasLastLightingBakeReport_ = false;
    };

} // namespace HIKARI
