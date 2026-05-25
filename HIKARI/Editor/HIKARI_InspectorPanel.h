#pragma once

namespace HIKARI {

    class AssetDatabase;
    class AssetRegistry;
    struct EditorSelection;
    class SceneCatalog;

    class InspectorPanel {
    public:
        void Draw(
            EditorSelection& selection,
            AssetRegistry* assetRegistry = nullptr,
            AssetDatabase* assetDatabase = nullptr,
            SceneCatalog* sceneCatalog = nullptr) const;

        void DrawContents(
            EditorSelection& selection,
            AssetRegistry* assetRegistry = nullptr,
            AssetDatabase* assetDatabase = nullptr,
            SceneCatalog* sceneCatalog = nullptr) const;
    };

} // namespace HIKARI
