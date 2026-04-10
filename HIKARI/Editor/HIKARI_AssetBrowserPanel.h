#pragma once

namespace HIKARI {

    struct EditorSelection;
    class ModelManager;

    class AssetBrowserPanel {
    public:
        void Draw(ModelManager& modelManager, EditorSelection& selection) const;
    };

} // namespace HIKARI
