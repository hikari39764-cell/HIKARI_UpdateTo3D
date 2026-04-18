#pragma once

namespace HIKARI {

    struct EditorSelection;
    class Camera3D;
    class ModelManager;
    class World;

    class StatsPanel {
    public:
        void Draw(const char* sceneName, const World& world, const ModelManager& modelManager, const EditorSelection& selection, const Camera3D& camera) const;
        void DrawContents(const char* sceneName, const World& world, const ModelManager& modelManager, const EditorSelection& selection, const Camera3D& camera) const;
    };

} // namespace HIKARI
