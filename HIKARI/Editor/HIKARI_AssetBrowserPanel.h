#pragma once

#include <array>
#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

    class AssetDatabase;
    struct AssetUsageSummary;
    struct EditorSelection;
    class ModelManager;

    enum class AssetBrowserScope {
        Project,
        CurrentScene,
        UnusedInScene,
        Broken,
        Textures,
        Models,
        Scenes,
        Materials,
        Skies,
        Vfx,
    };

    struct AssetBrowserContext {
        AssetGuid currentSceneGuid{};
        AssetGuid startupSceneGuid{};
        bool currentSceneDirty = false;
    };

    class AssetBrowserPanel {
    public:
        void Draw(ModelManager& modelManager, EditorSelection& selection) const;
        void Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const;
        void DrawContents(AssetDatabase& assetDatabase, EditorSelection& selection) const;
        void DrawContents(
            AssetDatabase& assetDatabase,
            EditorSelection& selection,
            const AssetUsageSummary* usageSummary,
            AssetBrowserScope scope,
            const AssetBrowserContext* context = nullptr) const;
        std::string ConsumeActivatedSceneGuid() const;
        std::string ConsumeSaveSceneAsGuid() const;

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
