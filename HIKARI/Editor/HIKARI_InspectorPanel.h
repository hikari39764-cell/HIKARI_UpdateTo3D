#pragma once

namespace HIKARI {

    struct EditorSelection;

    class InspectorPanel {
    public:
        void Draw(EditorSelection& selection) const;
        void DrawContents(EditorSelection& selection) const;
    };

} // namespace HIKARI
