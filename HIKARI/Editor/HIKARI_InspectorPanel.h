#pragma once

namespace HIKARI {

    class AssetDatabase;
    class AssetRegistry;
    struct EditorSelection;

    class InspectorPanel {
    public:
        void Draw(
            EditorSelection& selection,
            AssetRegistry* assetRegistry = nullptr,
            AssetDatabase* assetDatabase = nullptr) const;

        void DrawContents(
            EditorSelection& selection,
            AssetRegistry* assetRegistry = nullptr,
            AssetDatabase* assetDatabase = nullptr) const;
    };

} // namespace HIKARI
