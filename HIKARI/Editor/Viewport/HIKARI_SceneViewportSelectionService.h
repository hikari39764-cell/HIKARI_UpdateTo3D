#pragma once

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneObjectId.h"

struct ImDrawList;

namespace HIKARI {

    class Camera3D;

    namespace EDITOR {

        struct SceneViewportRect {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;

            bool Contains(const MATH::Vec2& point) const noexcept {
                return point.x >= x && point.y >= y &&
                    point.x <= x + width && point.y <= y + height;
            }
        };

        struct SceneViewportInteractionResult {
            SceneObjectId selection{};
            bool selectionChanged = false;
            bool openContextMenu = false;
        };

        class SceneViewportSelectionService {
        public:
            SceneObjectId PickObject(
                const Camera3D& camera,
                const SceneViewportRect& viewport,
                const MATH::Vec2& screenPosition);

            SceneViewportInteractionResult UpdateInput(
                const Camera3D& camera,
                const SceneViewportRect& viewport,
                const SceneViewportRect& blockedRegion,
                bool interactionEnabled,
                bool viewportHovered,
                bool gizmoCaptured);

            void DrawSelectionOutline(
                const Camera3D& camera,
                const SceneViewportRect& viewport,
                SceneObjectId objectId,
                ImDrawList* drawList) const;

            SceneObjectId GetContextTarget() const noexcept;

        private:
            struct SelectionAnchor {
                SceneObjectId objectId{};
                uint32_t surfaceIndex = 0;
                uint32_t nodeIndex = 0;
                uint32_t meshIndex = 0;
                uint32_t primitiveIndex = 0;
                MATH::Vec3 normalizedPosition{ 0.5f, 0.5f, 0.5f };
                bool hasSurface = false;
                bool valid = false;
            };

            bool contextPressActive_ = false;
            MATH::Vec2 contextPressPosition_{};
            SceneObjectId contextTarget_{};
            SelectionAnchor selectionAnchor_{};
        };

    } // namespace EDITOR
} // namespace HIKARI
