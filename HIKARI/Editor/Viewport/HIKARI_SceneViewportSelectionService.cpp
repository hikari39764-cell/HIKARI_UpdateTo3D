#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
        constexpr float kRayEpsilon = 1.0e-6f;

        struct SelectionRay {
            MATH::Vec3 origin{};
            MATH::Vec3 direction{};
        };

        SelectionRay BuildSelectionRay(
            const Camera3D& camera,
            const SceneViewportRect& viewport,
            const MATH::Vec2& screenPosition) {

            const float normalizedX =
                (screenPosition.x - viewport.x) /
                (std::max)(viewport.width, 1.0f);
            const float normalizedY =
                (screenPosition.y - viewport.y) /
                (std::max)(viewport.height, 1.0f);
            const float ndcX = normalizedX * 2.0f - 1.0f;
            const float ndcY = 1.0f - normalizedY * 2.0f;

            const MATH::Vec3 forward = MATH::Normalize(
                camera.GetTarget() - camera.GetPosition());
            const MATH::Vec3 right = MATH::Normalize(
                MATH::Cross(camera.GetUp(), forward));
            const MATH::Vec3 up = MATH::Normalize(
                MATH::Cross(forward, right));
            const float tanHalfFov = std::tan(
                (std::max)(camera.GetFovYRad(), 0.001f) * 0.5f);
            const float aspect = viewport.height > 0.0f
                ? viewport.width / viewport.height
                : camera.GetAspect();

            SelectionRay ray{};
            ray.origin = camera.GetPosition();
            ray.direction = MATH::Normalize(
                forward +
                right * (ndcX * aspect * tanHalfFov) +
                up * (ndcY * tanHalfFov));
            return ray;
        }

        bool IntersectRayBounds(
            const SelectionRay& ray,
            const Bounds& bounds,
            float& outDistance) {

            if (!BOUNDS::IsUsable(bounds)) {
                return false;
            }

            float nearDistance = 0.0f;
            float farDistance = (std::numeric_limits<float>::max)();
            const float origins[] = {
                ray.origin.x,
                ray.origin.y,
                ray.origin.z
            };
            const float directions[] = {
                ray.direction.x,
                ray.direction.y,
                ray.direction.z
            };
            const float minimums[] = {
                bounds.min.x,
                bounds.min.y,
                bounds.min.z
            };
            const float maximums[] = {
                bounds.max.x,
                bounds.max.y,
                bounds.max.z
            };

            for (int axis = 0; axis < 3; ++axis) {
                if (std::fabs(directions[axis]) <= kRayEpsilon) {
                    if (origins[axis] < minimums[axis] ||
                        origins[axis] > maximums[axis]) {
                        return false;
                    }
                    continue;
                }

                const float inverseDirection = 1.0f / directions[axis];
                float axisNear =
                    (minimums[axis] - origins[axis]) * inverseDirection;
                float axisFar =
                    (maximums[axis] - origins[axis]) * inverseDirection;
                if (axisNear > axisFar) {
                    std::swap(axisNear, axisFar);
                }
                nearDistance = (std::max)(nearDistance, axisNear);
                farDistance = (std::min)(farDistance, axisFar);
                if (nearDistance > farDistance) {
                    return false;
                }
            }

            outDistance = nearDistance;
            return farDistance >= 0.0f;
        }

#if defined(HIKARI_WITH_EDITOR)
        bool ProjectPoint(
            const Camera3D& camera,
            const SceneViewportRect& viewport,
            const MATH::Vec3& worldPosition,
            ImVec2& outScreenPosition) {

            const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({
                worldPosition.x,
                worldPosition.y,
                worldPosition.z,
                1.0f
            });
            if (clip.w <= kRayEpsilon) {
                return false;
            }

            const float inverseW = 1.0f / clip.w;
            const float depth = clip.z * inverseW;
            if (depth < 0.0f || depth > 1.0f) {
                return false;
            }

            const float ndcX = clip.x * inverseW;
            const float ndcY = clip.y * inverseW;
            outScreenPosition.x =
                viewport.x + (ndcX * 0.5f + 0.5f) * viewport.width;
            outScreenPosition.y =
                viewport.y + (-ndcY * 0.5f + 0.5f) * viewport.height;
            return true;
        }

        MATH::Vec3 NormalizePointInBounds(
            const MATH::Vec3& point,
            const Bounds& bounds) {

            const auto normalizeAxis = [](float value, float minimum, float maximum) {
                const float extent = maximum - minimum;
                if (std::fabs(extent) <= kRayEpsilon) {
                    return 0.5f;
                }
                return std::clamp((value - minimum) / extent, 0.0f, 1.0f);
            };
            return {
                normalizeAxis(point.x, bounds.min.x, bounds.max.x),
                normalizeAxis(point.y, bounds.min.y, bounds.max.y),
                normalizeAxis(point.z, bounds.min.z, bounds.max.z),
            };
        }

        MATH::Vec3 DenormalizePointInBounds(
            const MATH::Vec3& normalized,
            const Bounds& bounds) {

            return {
                bounds.min.x + (bounds.max.x - bounds.min.x) * normalized.x,
                bounds.min.y + (bounds.max.y - bounds.min.y) * normalized.y,
                bounds.min.z + (bounds.max.z - bounds.min.z) * normalized.z,
            };
        }

        float Cross2D(
            const ImVec2& origin,
            const ImVec2& first,
            const ImVec2& second) {

            return (first.x - origin.x) * (second.y - origin.y) -
                (first.y - origin.y) * (second.x - origin.x);
        }

        std::vector<ImVec2> BuildConvexHull(
            std::vector<ImVec2> points) {

            std::sort(
                points.begin(),
                points.end(),
                [](const ImVec2& lhs, const ImVec2& rhs) {
                    return lhs.x < rhs.x ||
                        (lhs.x == rhs.x && lhs.y < rhs.y);
                });
            points.erase(
                std::unique(
                    points.begin(),
                    points.end(),
                    [](const ImVec2& lhs, const ImVec2& rhs) {
                        return std::fabs(lhs.x - rhs.x) < 0.5f &&
                            std::fabs(lhs.y - rhs.y) < 0.5f;
                    }),
                points.end());
            if (points.size() < 3) {
                return points;
            }

            std::vector<ImVec2> hull(points.size() * 2u);
            size_t count = 0;
            for (const ImVec2& point : points) {
                while (count >= 2u &&
                    Cross2D(hull[count - 2u], hull[count - 1u], point) <=
                        0.0f) {
                    --count;
                }
                hull[count++] = point;
            }
            for (size_t index = points.size() - 1u, lowerCount = count + 1u;
                index > 0u;
                --index) {
                const ImVec2& point = points[index - 1u];
                while (count >= lowerCount &&
                    Cross2D(hull[count - 2u], hull[count - 1u], point) <=
                        0.0f) {
                    --count;
                }
                hull[count++] = point;
            }
            if (count > 1u) {
                --count;
            }
            hull.resize(count);
            return hull;
        }

        std::vector<ImVec2> BuildBoundsHull(
            const Camera3D& camera,
            const SceneViewportRect& viewport,
            const Bounds& bounds) {

            if (!BOUNDS::IsUsable(bounds)) {
                return {};
            }
            const std::array<MATH::Vec3, 8> corners = {{
                { bounds.min.x, bounds.min.y, bounds.min.z },
                { bounds.max.x, bounds.min.y, bounds.min.z },
                { bounds.min.x, bounds.max.y, bounds.min.z },
                { bounds.max.x, bounds.max.y, bounds.min.z },
                { bounds.min.x, bounds.min.y, bounds.max.z },
                { bounds.max.x, bounds.min.y, bounds.max.z },
                { bounds.min.x, bounds.max.y, bounds.max.z },
                { bounds.max.x, bounds.max.y, bounds.max.z },
            }};
            std::vector<ImVec2> projected;
            projected.reserve(corners.size());
            for (const MATH::Vec3& corner : corners) {
                ImVec2 screen{};
                if (ProjectPoint(camera, viewport, corner, screen)) {
                    projected.push_back(screen);
                }
            }
            return BuildConvexHull(std::move(projected));
        }

        bool IsOversizedHull(
            const std::vector<ImVec2>& hull,
            const SceneViewportRect& viewport) {

            if (hull.size() < 3u) {
                return true;
            }
            float minimumX = hull.front().x;
            float maximumX = hull.front().x;
            float minimumY = hull.front().y;
            float maximumY = hull.front().y;
            for (const ImVec2& point : hull) {
                minimumX = (std::min)(minimumX, point.x);
                maximumX = (std::max)(maximumX, point.x);
                minimumY = (std::min)(minimumY, point.y);
                maximumY = (std::max)(maximumY, point.y);
            }
            constexpr float kMaximumViewportCoverage = 0.82f;
            return maximumX - minimumX >
                    viewport.width * kMaximumViewportCoverage ||
                maximumY - minimumY >
                    viewport.height * kMaximumViewportCoverage;
        }

        void DrawSelectionHull(
            ImDrawList& drawList,
            const std::vector<ImVec2>& hull) {

            drawList.AddConvexPolyFilled(
                hull.data(),
                static_cast<int>(hull.size()),
                IM_COL32(54, 208, 224, 13));
            drawList.AddPolyline(
                hull.data(),
                static_cast<int>(hull.size()),
                IM_COL32(5, 12, 16, 220),
                ImDrawFlags_Closed,
                5.0f);
            drawList.AddPolyline(
                hull.data(),
                static_cast<int>(hull.size()),
                IM_COL32(73, 224, 235, 245),
                ImDrawFlags_Closed,
                2.0f);
        }

        void DrawSelectionAnchor(
            ImDrawList& drawList,
            const ImVec2& center) {

            constexpr float kRadius = 13.0f;
            constexpr float kCrossInner = 6.0f;
            constexpr float kCrossOuter = 19.0f;
            drawList.AddCircle(
                center,
                kRadius,
                IM_COL32(4, 12, 16, 235),
                24,
                5.0f);
            drawList.AddCircle(
                center,
                kRadius,
                IM_COL32(73, 224, 235, 255),
                24,
                2.0f);
            const ImU32 color = IM_COL32(73, 224, 235, 255);
            drawList.AddLine(
                { center.x - kCrossOuter, center.y },
                { center.x - kCrossInner, center.y },
                color,
                2.0f);
            drawList.AddLine(
                { center.x + kCrossInner, center.y },
                { center.x + kCrossOuter, center.y },
                color,
                2.0f);
            drawList.AddLine(
                { center.x, center.y - kCrossOuter },
                { center.x, center.y - kCrossInner },
                color,
                2.0f);
            drawList.AddLine(
                { center.x, center.y + kCrossInner },
                { center.x, center.y + kCrossOuter },
                color,
                2.0f);
        }
#endif
    }

    SceneObjectId SceneViewportSelectionService::PickObject(
        const Camera3D& camera,
        const SceneViewportRect& viewport,
        const MATH::Vec2& screenPosition) {

        selectionAnchor_ = {};

        if (viewport.width <= 0.0f ||
            viewport.height <= 0.0f ||
            !viewport.Contains(screenPosition)) {
            return {};
        }

        const SelectionRay ray = BuildSelectionRay(
            camera,
            viewport,
            screenPosition);
        const RENDER3D::RUNTIME::SceneRenderCache& cache =
            RenderSubmissionSystem::GetSceneRenderCache();

        SceneObjectId selectedObject{};
        float selectedDistance = (std::numeric_limits<float>::max)();
        const RENDER3D::RUNTIME::SceneSurfaceInstance* selectedSurface =
            nullptr;
        for (const RENDER3D::RUNTIME::SceneSurfaceInstance& surface :
            cache.GetSurfaceInstances()) {
            if (!surface.valid ||
                !surface.visible ||
                !surface.objectId.IsValid()) {
                continue;
            }
            float distance = 0.0f;
            if (IntersectRayBounds(ray, surface.worldBounds, distance) &&
                distance < selectedDistance) {
                selectedDistance = distance;
                selectedObject.value = surface.objectId.value;
                selectedSurface = &surface;
            }
        }

        if (selectedObject.value != 0u) {
            const MATH::Vec3 hitPoint =
                ray.origin + ray.direction * selectedDistance;
            selectionAnchor_.objectId = selectedObject;
            selectionAnchor_.surfaceIndex = selectedSurface->surfaceIndex;
            selectionAnchor_.nodeIndex = selectedSurface->nodeIndex;
            selectionAnchor_.meshIndex = selectedSurface->meshIndex;
            selectionAnchor_.primitiveIndex = selectedSurface->primitiveIndex;
            selectionAnchor_.normalizedPosition = NormalizePointInBounds(
                hitPoint,
                selectedSurface->worldBounds);
            selectionAnchor_.hasSurface = true;
            selectionAnchor_.valid = true;
            return selectedObject;
        }

        for (const RENDER3D::RUNTIME::SceneRenderObject& object :
            cache.GetObjects()) {
            if (!object.valid ||
                !object.desc.visible ||
                !object.desc.id.IsValid()) {
                continue;
            }
            float distance = 0.0f;
            if (IntersectRayBounds(ray, object.desc.worldBounds, distance) &&
                distance < selectedDistance) {
                selectedDistance = distance;
                selectedObject.value = object.desc.id.value;
            }
        }
        if (selectedObject.value != 0u) {
            const RENDER3D::RUNTIME::SceneRenderObject* object =
                cache.Find(RENDER3D::RUNTIME::SceneRenderObjectId{
                    selectedObject.value });
            if (object != nullptr) {
                const MATH::Vec3 hitPoint =
                    ray.origin + ray.direction * selectedDistance;
                selectionAnchor_.objectId = selectedObject;
                selectionAnchor_.normalizedPosition = NormalizePointInBounds(
                    hitPoint,
                    object->desc.worldBounds);
                selectionAnchor_.valid = true;
            }
        }
        return selectedObject;
    }

    SceneViewportInteractionResult
        SceneViewportSelectionService::UpdateInput(
            const Camera3D& camera,
            const SceneViewportRect& viewport,
            const SceneViewportRect& blockedRegion,
            bool interactionEnabled,
            bool viewportHovered,
            bool gizmoCaptured) {

        SceneViewportInteractionResult result{};
#if defined(HIKARI_WITH_EDITOR)
        const ImVec2 mouse = ImGui::GetMousePos();
        const MATH::Vec2 mousePosition{ mouse.x, mouse.y };
        const bool blocked = blockedRegion.Contains(mousePosition);

        if (interactionEnabled &&
            viewportHovered &&
            !gizmoCaptured &&
            !blocked &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            result.selection = PickObject(
                camera,
                viewport,
                mousePosition);
            result.selectionChanged = true;
        }

        if (interactionEnabled &&
            viewportHovered &&
            !gizmoCaptured &&
            !blocked &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            contextPressActive_ = true;
            contextPressPosition_ = mousePosition;
        }

        if (contextPressActive_ &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            const float deltaX =
                mousePosition.x - contextPressPosition_.x;
            const float deltaY =
                mousePosition.y - contextPressPosition_.y;
            const float dragDistanceSquared =
                deltaX * deltaX + deltaY * deltaY;
            if (interactionEnabled &&
                viewport.Contains(mousePosition) &&
                !blocked &&
                dragDistanceSquared <= 16.0f) {
                contextTarget_ = PickObject(
                    camera,
                    viewport,
                    mousePosition);
                result.selection = contextTarget_;
                result.selectionChanged = true;
                result.openContextMenu = true;
            }
            contextPressActive_ = false;
        }
#else
        (void)camera;
        (void)viewport;
        (void)blockedRegion;
        (void)interactionEnabled;
        (void)viewportHovered;
        (void)gizmoCaptured;
#endif
        return result;
    }

    void SceneViewportSelectionService::DrawSelectionOutline(
        const Camera3D& camera,
        const SceneViewportRect& viewport,
        SceneObjectId objectId,
        ImDrawList* drawList) const {
#if defined(HIKARI_WITH_EDITOR)
        if (objectId.value == 0u ||
            drawList == nullptr ||
            viewport.width <= 0.0f ||
            viewport.height <= 0.0f) {
            return;
        }

        const RENDER3D::RUNTIME::SceneRenderObject* object =
            RenderSubmissionSystem::GetSceneRenderCache().Find(
                RENDER3D::RUNTIME::SceneRenderObjectId{ objectId.value });
        if (object == nullptr ||
            !object->valid ||
            !BOUNDS::IsUsable(object->desc.worldBounds)) {
            return;
        }

        const std::vector<ImVec2> objectHull = BuildBoundsHull(
            camera,
            viewport,
            object->desc.worldBounds);
        if (!IsOversizedHull(objectHull, viewport)) {
            DrawSelectionHull(*drawList, objectHull);
            return;
        }

        const RENDER3D::RUNTIME::SceneRenderCache& cache =
            RenderSubmissionSystem::GetSceneRenderCache();
        const RENDER3D::RUNTIME::SceneSurfaceInstance* anchorSurface = nullptr;
        if (selectionAnchor_.valid &&
            selectionAnchor_.hasSurface &&
            selectionAnchor_.objectId.value == objectId.value) {
            for (const RENDER3D::RUNTIME::SceneSurfaceInstance& surface :
                cache.GetSurfaceInstances()) {
                if (surface.valid &&
                    surface.objectId.value == objectId.value &&
                    surface.surfaceIndex == selectionAnchor_.surfaceIndex &&
                    surface.nodeIndex == selectionAnchor_.nodeIndex &&
                    surface.meshIndex == selectionAnchor_.meshIndex &&
                    surface.primitiveIndex == selectionAnchor_.primitiveIndex) {
                    anchorSurface = &surface;
                    break;
                }
            }
        }

        if (anchorSurface != nullptr) {
            const std::vector<ImVec2> surfaceHull = BuildBoundsHull(
                camera,
                viewport,
                anchorSurface->worldBounds);
            if (!IsOversizedHull(surfaceHull, viewport)) {
                DrawSelectionHull(*drawList, surfaceHull);
                return;
            }
        }

        const Bounds& anchorBounds = anchorSurface != nullptr
            ? anchorSurface->worldBounds
            : object->desc.worldBounds;
        const MATH::Vec3 normalizedAnchor =
            selectionAnchor_.valid &&
                selectionAnchor_.objectId.value == objectId.value
            ? selectionAnchor_.normalizedPosition
            : MATH::Vec3{ 0.5f, 0.5f, 0.5f };
        ImVec2 anchorScreen{};
        if (ProjectPoint(
                camera,
                viewport,
                DenormalizePointInBounds(normalizedAnchor, anchorBounds),
                anchorScreen) &&
            anchorScreen.x >= viewport.x &&
            anchorScreen.x <= viewport.x + viewport.width &&
            anchorScreen.y >= viewport.y &&
            anchorScreen.y <= viewport.y + viewport.height) {
            DrawSelectionAnchor(*drawList, anchorScreen);
        }
#else
        (void)camera;
        (void)viewport;
        (void)objectId;
        (void)drawList;
#endif
    }

    SceneObjectId
        SceneViewportSelectionService::GetContextTarget() const noexcept {
        return contextTarget_;
    }

} // namespace HIKARI::EDITOR
