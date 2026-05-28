#pragma once

#include <string>

#include "Editor/HIKARI_AssetBrowserPanel.h"
#include "Editor/Panels/HIKARI_AssetInspectorPanel.h"

namespace HIKARI {

    class AssetDatabase;
    class AssetRegistry;
    struct EditorSelection;
    struct SceneDocument;

    struct ResourceWorkspaceContext {
        AssetGuid currentSceneGuid{};
        AssetGuid startupSceneGuid{};
        bool currentSceneDirty = false;
    };

    class ResourceWorkspacePanel {
    public:
        void Draw(
            AssetDatabase& assetDatabase,
            AssetRegistry& assetRegistry,
            const SceneDocument& sceneDocument,
            EditorSelection& selection,
            const ResourceWorkspaceContext& context) const;
        std::string ConsumeActivatedSceneGuid() const;
        std::string ConsumeSaveSceneAsGuid() const;
        std::string ConsumeRefreshRuntimeAssetGuid() const;
        std::string ConsumeReimportAndRefreshRuntimeAssetGuid() const;
        bool ConsumeRefreshCurrentSceneResourcesRequested() const;

    private:
        mutable AssetBrowserPanel assetBrowserPanel_{};
        mutable AssetInspectorPanel assetInspectorPanel_{};
        mutable bool showPreviewLog_ = false;
        mutable bool refreshCurrentSceneResourcesRequested_ = false;
        mutable AssetBrowserScope activeScope_ = AssetBrowserScope::Project;
    };

} // namespace HIKARI
