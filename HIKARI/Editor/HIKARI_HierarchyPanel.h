#pragma once

namespace HIKARI {

    struct EditorSelection;
    class World;

    class HierarchyPanel {
    public:
        void Draw(World& world, EditorSelection& selection) const;
    };

} // namespace HIKARI
