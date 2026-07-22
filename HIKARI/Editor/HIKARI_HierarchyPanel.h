#pragma once

#include <functional>

namespace HIKARI {

    struct EditorSelection;
    class GameObject;
    class World;

    class HierarchyPanel {
    public:
        void Draw(World& world, EditorSelection& selection) const;
        void DrawContents(World& world, EditorSelection& selection) const;
        void DrawContents(
            World& world,
            EditorSelection& selection,
            const std::function<void(GameObject&)>&
                drawObjectContextMenu) const;
    };

} // namespace HIKARI
