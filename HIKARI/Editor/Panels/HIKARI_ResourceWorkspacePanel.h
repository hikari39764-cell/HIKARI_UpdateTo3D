#pragma once

#include <string>

#include "Editor/HIKARI_AssetBrowserPanel.h"
#include "Editor/Panels/HIKARI_AssetInspectorPanel.h"

namespace HIKARI {

    class AssetDatabase;
    struct EditorSelection;
    struct SceneDocument;

    class ResourceWorkspacePanel {
    public:
        void Draw(
            AssetDatabase& assetDatabase,
            const SceneDocument& sceneDocument,
            EditorSelection& selection) const;
        std::string ConsumeActivatedSceneGuid() const;

    private:
        mutable AssetBrowserPanel assetBrowserPanel_{};
        mutable AssetInspectorPanel assetInspectorPanel_{};
        mutable bool showPreviewLog_ = false;
        mutable AssetBrowserScope activeScope_ = AssetBrowserScope::Project;
    };

} // namespace HIKARI
