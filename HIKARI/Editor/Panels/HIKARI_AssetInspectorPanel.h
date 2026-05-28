#pragma once

namespace HIKARI {

    class AssetDatabase;
    class AssetRegistry;
    struct EditorSelection;

    class AssetInspectorPanel {
    public:
        void Draw(AssetDatabase& assetDatabase, AssetRegistry& assetRegistry, EditorSelection& selection) const;
    };

} // namespace HIKARI
