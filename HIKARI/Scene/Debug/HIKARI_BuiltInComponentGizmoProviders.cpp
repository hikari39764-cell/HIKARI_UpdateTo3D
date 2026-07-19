#include "Scene/Debug/HIKARI_BuiltInComponentGizmoProviders.h"

#include <array>
#include <cmath>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_PlayerControllerComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
    namespace {
        constexpr unsigned int kSpawnColor = 0x55FF66FF;
        constexpr unsigned int kPlayerBoundsColor = 0x43D9FFFF;
        constexpr unsigned int kPlayerBoundsCornerColor = 0x96FF8AFF;
        constexpr unsigned int kCameraFrustumColor = 0x65D9FFFF;
        constexpr unsigned int kSelectedCameraFrustumColor = 0xFFD166FF;

        void SubmitArrow(
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            unsigned int color) {

            RENDERER3D::DEBUG::SubmitLine3D(
                RENDERER3D::DEBUG::Line3D{ from, to, color });
            const MATH::Vec3 dir = MATH::Normalize(to - from);
            const MATH::Vec3 sideA = { -dir.z, 0.0f, dir.x };
            const MATH::Vec3 headBase = to - dir * 0.25f;
            RENDERER3D::DEBUG::SubmitLine3D(
                RENDERER3D::DEBUG::Line3D{
                    to,
                    headBase + sideA * 0.12f,
                    color });
            RENDERER3D::DEBUG::SubmitLine3D(
                RENDERER3D::DEBUG::Line3D{
                    to,
                    headBase - sideA * 0.12f,
                    color });
        }

        void SubmitXRayLine(
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            unsigned int color) {

            RENDERER3D::DEBUG::Line3D line{};
            line.from = from;
            line.to = to;
            line.rgba = color;
            line.depthMode =
                RENDERER3D::DEBUG::DebugDepthMode::XRay;
            RENDERER3D::DEBUG::SubmitLine3D(line);
        }

        void DrawPlayerBounds(
            const GameObject& object,
            const ComponentGizmoDrawContext&) {

            const PlayerControllerComponent* player =
                object.GetComponent<PlayerControllerComponent>();
            if (player == nullptr || !player->IsEnabled() ||
                !player->GetUseBounds()) {
                return;
            }

            const Transform3D& transform = object.GetTransform();
            const float minX = player->GetMinX();
            const float maxX = player->GetMaxX();
            const float minZ = player->GetMinZ();
            const float maxZ = player->GetMaxZ();
            const float y = transform.position.y + 0.08f;
            const MATH::Vec3 p00{ minX, y, minZ };
            const MATH::Vec3 p10{ maxX, y, minZ };
            const MATH::Vec3 p11{ maxX, y, maxZ };
            const MATH::Vec3 p01{ minX, y, maxZ };
            SubmitXRayLine(p00, p10, kPlayerBoundsColor);
            SubmitXRayLine(p10, p11, kPlayerBoundsColor);
            SubmitXRayLine(p11, p01, kPlayerBoundsColor);
            SubmitXRayLine(p01, p00, kPlayerBoundsColor);

            const MATH::Vec3 height{ 0.0f, 0.6f, 0.0f };
            SubmitXRayLine(p00, p00 + height, kPlayerBoundsCornerColor);
            SubmitXRayLine(p10, p10 + height, kPlayerBoundsCornerColor);
            SubmitXRayLine(p11, p11 + height, kPlayerBoundsCornerColor);
            SubmitXRayLine(p01, p01 + height, kPlayerBoundsCornerColor);

            const MATH::Vec3 center{
                (minX + maxX) * 0.5f,
                y,
                (minZ + maxZ) * 0.5f
            };
            SubmitXRayLine(
                { minX, y, center.z },
                { maxX, y, center.z },
                0x43D9FF88);
            SubmitXRayLine(
                { center.x, y, minZ },
                { center.x, y, maxZ },
                0x43D9FF88);
        }

        MATH::Vec3 TransformFrustumPoint(
            const MATH::Mat4& world,
            float x,
            float y,
            float z) {

            const MATH::Vec4 point =
                world.TransformPoint({ x, y, z, 1.0f });
            return { point.x, point.y, point.z };
        }

        void DrawCameraFrustum(
            const GameObject& object,
            const ComponentGizmoDrawContext& context) {

            const CameraComponent* camera =
                object.GetComponent<CameraComponent>();
            if (camera == nullptr || !camera->IsEnabled()) {
                return;
            }

            const float aspect = std::isfinite(context.cameraAspect) &&
                context.cameraAspect > 0.0001f
                ? context.cameraAspect
                : 16.0f / 9.0f;
            const float nearDistance = camera->GetNearClip();
            const float farDistance = (std::min)(
                camera->GetFarClip(),
                (std::max)(8.0f, nearDistance * 4.0f));
            const float tangent =
                std::tan(camera->GetFovYRad() * 0.5f);
            const float nearHalfHeight = tangent * nearDistance;
            const float nearHalfWidth = nearHalfHeight * aspect;
            const float farHalfHeight = tangent * farDistance;
            const float farHalfWidth = farHalfHeight * aspect;
            const MATH::Mat4 world =
                object.GetTransform().GetWorldMatrix();

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
            const unsigned int color =
                object.GetDocumentId() == context.selectedObjectId
                ? kSelectedCameraFrustumColor
                : kCameraFrustumColor;
            for (const auto& edge : kEdges) {
                RENDERER3D::DEBUG::SubmitLine3D(
                    RENDERER3D::DEBUG::Line3D{
                        corners[edge[0]],
                        corners[edge[1]],
                        color });
            }
        }

        void DrawSpawnPoint(
            const GameObject& object,
            const ComponentGizmoDrawContext&) {

            const SpawnPointComponent* spawn =
                object.GetComponent<SpawnPointComponent>();
            if (spawn == nullptr || !spawn->IsEnabled()) {
                return;
            }
            const Transform3D& transform = object.GetTransform();
            RENDERER3D::DEBUG::WireCube marker{};
            marker.transform = transform;
            marker.size = 0.35f;
            marker.rgba = kSpawnColor;
            RENDERER3D::DEBUG::SubmitWireCube(marker);
            SubmitArrow(
                transform.position,
                transform.position + MATH::Vec3{ 0.0f, 0.8f, 0.0f },
                kSpawnColor);
        }
    }

    void RegisterBuiltInComponentGizmoProviders(
        ComponentGizmoRegistry& registry) {

        (void)registry.Register(ComponentGizmoProvider{
            std::string(kCameraFrustumGizmoProviderId),
            "Camera Frustums",
            true,
            DrawCameraFrustum
        });
        (void)registry.Register(ComponentGizmoProvider{
            std::string(kSpawnPointGizmoProviderId),
            "Spawn Points",
            false,
            DrawSpawnPoint
        });
        (void)registry.Register(ComponentGizmoProvider{
            std::string(kPlayerBoundsGizmoProviderId),
            "Player Bounds",
            true,
            DrawPlayerBounds
        });
    }

} // namespace HIKARI
