#pragma once

#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_SceneCatalog.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"

namespace HIKARI {

    class DocumentSceneBase : public IScene {
    public:
        DocumentSceneBase(SceneCatalog& sceneCatalog, std::string sceneId);

        void OnEnter() override;
        void OnExit() override;
        void Update(float dt) override;
        void Render() override;
        void RenderImGui() override;

        const std::string& GetSceneId() const override;
        const std::string& GetScenePath() const;
        SceneCatalog& GetSceneCatalog();
        const SceneCatalog& GetSceneCatalog() const;
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
        ModelManager& GetModelManager();
        SkyManager& GetSkyManager();
        ComponentRegistry& GetComponentRegistry();

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
        bool RefreshSkyRuntime();
        bool SaveCurrentSceneDocument();
        bool SaveCurrentSceneDocumentAs(const AssetGuid& sceneGuid);
        const AssetGuid& GetCurrentSceneAssetGuid() const;
        bool IsCurrentSceneAsset(const AssetGuid& guid) const;
        std::string GetCurrentSceneDisplayName() const;

        void SetComponentGizmoState(const ComponentGizmoState& state);
        void SetViewportOverlayState(const ViewportOverlayState& state);
        void SetSelectedGizmoObjectId(SceneObjectId id);

    protected:
        void RegisterDefaultComponentTypes();
        void RegisterDefaultSceneCatalogEntries();
        void RegisterDefaultSystems();

        virtual bool UseDebugCamera() const;
        virtual bool DrawDebugHelpers() const;
        virtual bool UseEnvironmentLighting() const;
        void ConfigureModelTextureResolver();
        std::string ResolveModelTexturePathFromAssets(
            const std::string& sourceTexturePath,
            ModelTextureUsage usage) const;
        const AssetRecord* FindUniqueTextureAssetByFilename(
            const std::string& filename,
            const std::string& sourceTexturePath) const;

    protected:
        SceneCatalog& sceneCatalog_;
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
        ComponentGizmoRenderer componentGizmoRenderer_{};
        ComponentGizmoState componentGizmoState_{};
        ViewportOverlayState viewportOverlayState_{};
        SceneObjectId selectedGizmoObjectId_{};
    };

} // namespace HIKARI
