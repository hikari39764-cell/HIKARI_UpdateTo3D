#pragma once

#include <string>
#include <memory>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Gizmos/HIKARI_LightProbeVolumeGizmoRenderer.h"
#include "Editor/Gizmos/HIKARI_ReflectionProbeGizmoRenderer.h"
#endif
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_World.h"
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
        DebugCameraController3D& GetDebugCamera();

        bool& GetEnvironmentLightingEnabled();
        bool ReloadAssets();
        bool ReloadSceneDocument();
        bool RebuildRuntimeWorld();
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
        void SetClusteredGeometryPreviewState(const ClusteredGeometryPreviewState& state);
        void SetViewportGizmoInteracting(bool interacting);
        void SetSelectedGizmoObjectId(SceneObjectId id);
        void SyncReflectionProbeRuntimeFromAuthoring();

    protected:
        void RegisterDefaultComponentTypes();
        void RegisterDefaultSystems();

        virtual bool UseDebugCamera() const;
        virtual bool DrawDebugHelpers() const;
        virtual bool UseEnvironmentLighting() const;
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
        DebugCameraController3D debugCamera_{};
        World world_{};
        SystemScheduler systemScheduler_{};
        ModelManager modelManager_{};
        SkyManager skyManager_{};
        SceneEnvironment environment_{};
        bool environmentLightingEnabled_ = true;

        AssetDatabase assetDatabase_{};
        AssetRegistry assetRegistry_{};
        ComponentRegistry componentRegistry_{};
        SceneSerializer sceneSerializer_{};
        SceneRuntimeBuilder runtimeBuilder_{};
        SceneDocument sceneDocument_{};
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
        ClusteredGeometryPreviewState clusteredGeometryPreviewState_{};
        bool viewportGizmoInteracting_ = false;
        SceneObjectId selectedGizmoObjectId_{};
        std::unique_ptr<ReflectionProbeBakeJob> reflectionProbeBakeJob_{};
        std::unique_ptr<LightProbeBakeJob> lightProbeBakeJob_{};
        TOOLS::BAKING::LightingBakeReport lastLightingBakeReport_{};
        bool hasLastLightingBakeReport_ = false;
    };

} // namespace HIKARI
