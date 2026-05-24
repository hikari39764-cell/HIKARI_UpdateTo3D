#pragma once

#include <filesystem>

namespace HIKARI {

    class AssetDatabase;
    struct EditorSelection;
    class ModelManager;

    class AssetBrowserPanel {
    public:
        void Draw(ModelManager& modelManager, EditorSelection& selection) const;
        void Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const;

    private:
        mutable std::filesystem::path currentDirectory_{ "Assets" };
    };

} // namespace HIKARI
