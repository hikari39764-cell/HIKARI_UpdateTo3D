#pragma once

namespace HIKARI {

    class AssetDatabase;
    struct EditorSelection;

    class AssetInspectorPanel {
    public:
        void Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const;
    };

} // namespace HIKARI
