#include "HIKARI_ComponentGizmoRenderer.h"

#include <array>
#include <cmath>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_PlayerControllerComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"

namespace HIKARI {
    namespace {
        constexpr unsigned int kSpawnColor = 0x55FF66FF;
        constexpr unsigned int kPlayerBoundsColor = 0x43D9FFFF;
        constexpr unsigned int kPlayerBoundsCornerColor = 0x96FF8AFF;
        constexpr unsigned int kCameraFrustumColor = 0x65D9FFFF;
        constexpr unsigned int kSelectedCameraFrustumColor = 0xFFD166FF;

        bool ShouldDrawForObject(const GameObject& object, const ComponentGizmoState& state, SceneObjectId selectedObjectId) {
            if (!state.showOnlySelectedObject) {
                return true;
            }
            return selectedObjectId.value != 0 && object.GetDocumentId() == selectedObjectId;
        }

        void SubmitArrow(const MATH::Vec3& from, const MATH::Vec3& to, unsigned int color) {
            RENDERER3D::DEBUG::SubmitLine3D(RENDERER3D::DEBUG::Line3D{ from, to, color });
            const MATH::Vec3 dir = MATH::Normalize(to - from);
            const MATH::Vec3 sideA = { -dir.z, 0.0f, dir.x };
            const MATH::Vec3 headBase = to - dir * 0.25f;
            RENDERER3D::DEBUG::SubmitLine3D(RENDERER3D::DEBUG::Line3D{ to, headBase + sideA * 0.12f, color });
            RENDERER3D::DEBUG::SubmitLine3D(RENDERER3D::DEBUG::Line3D{ to, headBase - sideA * 0.12f, color });
        }

        void SubmitXRayLine(const MATH::Vec3& from, const MATH::Vec3& to, unsigned int color) {
            RENDERER3D::DEBUG::Line3D line{};
            line.from = from;
            line.to = to;
            line.rgba = color;
            line.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
            RENDERER3D::DEBUG::SubmitLine3D(line);
        }

        void SubmitPlayerBounds(const PlayerControllerComponent& player, const Transform3D& transform) {
            const float minX = player.GetMinX();
            const float maxX = player.GetMaxX();
            const float minZ = player.GetMinZ();
            const float maxZ = player.GetMaxZ();
            const float y = transform.position.y + 0.08f;

            const MATH::Vec3 p00{ minX, y, minZ };
            const MATH::Vec3 p10{ maxX, y, minZ };
            const MATH::Vec3 p11{ maxX, y, maxZ };
            const MATH::Vec3 p01{ minX, y, maxZ };
            SubmitXRayLine(p00, p10, kPlayerBoundsColor);
            SubmitXRayLine(p10, p11, kPlayerBoundsColor);
            SubmitXRayLine(p11, p01, kPlayerBoundsColor);
            SubmitXRayLine(p01, p00, kPlayerBoundsColor);

            const float tickHeight = 0.6f;
            SubmitXRayLine(p00, p00 + MATH::Vec3{ 0.0f, tickHeight, 0.0f }, kPlayerBoundsCornerColor);
            SubmitXRayLine(p10, p10 + MATH::Vec3{ 0.0f, tickHeight, 0.0f }, kPlayerBoundsCornerColor);
            SubmitXRayLine(p11, p11 + MATH::Vec3{ 0.0f, tickHeight, 0.0f }, kPlayerBoundsCornerColor);
            SubmitXRayLine(p01, p01 + MATH::Vec3{ 0.0f, tickHeight, 0.0f }, kPlayerBoundsCornerColor);

            const MATH::Vec3 center{
                (minX + maxX) * 0.5f,
                y,
                (minZ + maxZ) * 0.5f
            };
            SubmitXRayLine({ minX, y, center.z }, { maxX, y, center.z }, 0x43D9FF88);
            SubmitXRayLine({ center.x, y, minZ }, { center.x, y, maxZ }, 0x43D9FF88);
        }

        MATH::Vec3 TransformFrustumPoint(
            const MATH::Mat4& world,
            float x,
            float y,
            float z) {

            const MATH::Vec4 point = world.TransformPoint({ x, y, z, 1.0f });
            return { point.x, point.y, point.z };
        }

        void SubmitCameraFrustum(
            const CameraComponent& camera,
            const Transform3D& transform,
            float aspect,
            unsigned int color) {

            if (!camera.IsEnabled()) {
                return;
            }
            const float safeAspect = std::isfinite(aspect) && aspect > 0.0001f
                ? aspect
                : 16.0f / 9.0f;
            const float nearDistance = camera.GetNearClip();
            const float farDistance = (std::min)(
                camera.GetFarClip(),
                (std::max)(8.0f, nearDistance * 4.0f));
            const float tangent = std::tan(camera.GetFovYRad() * 0.5f);
            const float nearHalfHeight = tangent * nearDistance;
            const float nearHalfWidth = nearHalfHeight * safeAspect;
            const float farHalfHeight = tangent * farDistance;
            const float farHalfWidth = farHalfHeight * safeAspect;
            const MATH::Mat4 world = transform.GetWorldMatrix();

            const std::array<MATH::Vec3, 8> corners{ {
                TransformFrustumPoint(world, -nearHalfWidth, -nearHalfHeight, nearDistance),
                TransformFrustumPoint(world,  nearHalfWidth, -nearHalfHeight, nearDistance),
                TransformFrustumPoint(world,  nearHalfWidth,  nearHalfHeight, nearDistance),
                TransformFrustumPoint(world, -nearHalfWidth,  nearHalfHeight, nearDistance),
                TransformFrustumPoint(world, -farHalfWidth, -farHalfHeight, farDistance),
                TransformFrustumPoint(world,  farHalfWidth, -farHalfHeight, farDistance),
                TransformFrustumPoint(world,  farHalfWidth,  farHalfHeight, farDistance),
                TransformFrustumPoint(world, -farHalfWidth,  farHalfHeight, farDistance),
            } };
            constexpr std::array<std::array<int, 2>, 12> kEdges{ {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            } };
            for (const auto& edge : kEdges) {
                RENDERER3D::DEBUG::SubmitLine3D(
                    RENDERER3D::DEBUG::Line3D{ corners[edge[0]], corners[edge[1]], color });
            }
        }
    }

    void ComponentGizmoRenderer::SubmitWorldGizmos(
        const World& world,
        const ComponentGizmoState& state,
        SceneObjectId selectedObjectId,
        float cameraAspect) const {
        if (!state.showComponentGizmos) {
            return;
        }

        for (const auto& object : world.GetObjects()) {
            if (!object) {
                continue;
            }
            if (!ShouldDrawForObject(*object, state, selectedObjectId)) {
                continue;
            }

            const Transform3D& transform = object->Transform();

            const CameraComponent* camera = object->GetComponent<CameraComponent>();
            if (camera && state.showCameraFrustums) {
                const unsigned int color = object->GetDocumentId() == selectedObjectId
                    ? kSelectedCameraFrustumColor
                    : kCameraFrustumColor;
                SubmitCameraFrustum(*camera, transform, cameraAspect, color);
            }

            const SpawnPointComponent* spawn = object->GetComponent<SpawnPointComponent>();
            if (spawn && spawn->IsEnabled() && state.showSpawnPoints) {
                RENDERER3D::DEBUG::WireCube marker{};
                marker.transform = transform;
                marker.size = 0.35f;
                marker.rgba = kSpawnColor;
                RENDERER3D::DEBUG::SubmitWireCube(marker);
                SubmitArrow(transform.position, transform.position + MATH::Vec3{ 0.0f, 0.8f, 0.0f }, kSpawnColor);
            }

            const PlayerControllerComponent* player = object->GetComponent<PlayerControllerComponent>();
            if (player && player->IsEnabled() && player->GetUseBounds() && state.showPlayerBounds) {
                SubmitPlayerBounds(*player, transform);
            }
        }
    }

} // namespace HIKARI
