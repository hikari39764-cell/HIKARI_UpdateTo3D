#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace HIKARI {

    class AssetDatabase;
    struct EditorSelection;
    class ModelManager;

    class AssetBrowserPanel {
    public:
        void Draw(ModelManager& modelManager, EditorSelection& selection) const;
        void Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const;
        void DrawContents(AssetDatabase& assetDatabase, EditorSelection& selection) const;

    private:
        mutable std::filesystem::path currentDirectory_{ "Assets" };
        mutable std::array<char, 128> searchBuffer_{};
        mutable int typeFilter_ = 0;
        mutable int stateFilter_ = 0;
        mutable int viewMode_ = 0;
        mutable bool recursive_ = false;
        mutable std::string lastOperationMessage_{};
    };

} // namespace HIKARI
