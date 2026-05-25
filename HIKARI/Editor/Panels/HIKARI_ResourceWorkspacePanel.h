#pragma once

#include "Editor/HIKARI_AssetBrowserPanel.h"
#include "Editor/Panels/HIKARI_AssetInspectorPanel.h"

namespace HIKARI {

    class AssetDatabase;
    struct EditorSelection;

    class ResourceWorkspacePanel {
    public:
        void Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const;

    private:
        mutable AssetBrowserPanel assetBrowserPanel_{};
        mutable AssetInspectorPanel assetInspectorPanel_{};
        mutable bool showPreviewLog_ = false;
    };

} // namespace HIKARI
