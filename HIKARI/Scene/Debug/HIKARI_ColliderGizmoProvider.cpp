#include "Scene/Debug/HIKARI_ColliderGizmoProvider.h"

#include <array>
#include <cmath>
#include <string>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
    namespace {
        constexpr unsigned int kColliderColor = 0x4DA6FFFF;
        constexpr unsigned int kSelectedColliderColor = 0xFFD166FF;
        constexpr unsigned int kTriggerColor = 0x55FF99FF;
        constexpr int kCircleSegments = 24;

        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& point) {
            const MATH::Vec4 transformed = matrix.TransformPoint(
                { point.x, point.y, point.z, 1.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        void SubmitLine(
            const MATH::Mat4& matrix,
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            unsigned int color,
            bool selected) {
            RENDERER3D::DEBUG::Line3D line{};
            line.from = TransformPoint(matrix, from);
            line.to = TransformPoint(matrix, to);
            line.rgba = color;
            line.depthMode = selected
                ? RENDERER3D::DEBUG::DebugDepthMode::XRay
                : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;
            RENDERER3D::DEBUG::SubmitLine3D(line);
        }

        void SubmitCircle(
            const MATH::Mat4& matrix,
            int axis,
            float radius,
            float axisOffset,
            unsigned int color,
            bool selected) {
            constexpr float kTwoPi = 6.2831853071795864769f;
            for (int i = 0; i < kCircleSegments; ++i) {
                const float angleA = kTwoPi *
                    static_cast<float>(i) /
                    static_cast<float>(kCircleSegments);
                const float angleB = kTwoPi *
                    static_cast<float>(i + 1) /
                    static_cast<float>(kCircleSegments);
                MATH::Vec3 a{};
                MATH::Vec3 b{};
                const auto writePoint = [axis, radius, axisOffset](
                    float angle,
                    MATH::Vec3& point) {
                    const float c = std::cos(angle) * radius;
                    const float s = std::sin(angle) * radius;
                    if (axis == 0) {
                        point = { axisOffset, c, s };
                    } else if (axis == 1) {
                        point = { c, axisOffset, s };
                    } else {
                        point = { c, s, axisOffset };
                    }
                };
                writePoint(angleA, a);
                writePoint(angleB, b);
                SubmitLine(matrix, a, b, color, selected);
            }
        }

        void SubmitCapsuleArc(
            const MATH::Mat4& matrix,
            int radialAxis,
            float radius,
            float cylinderHalfHeight,
            float side,
            unsigned int color,
            bool selected) {
            constexpr float kPi = 3.14159265358979323846f;
            const int segments = kCircleSegments / 2;
            for (int i = 0; i < segments; ++i) {
                const float a0 = kPi * static_cast<float>(i) /
                    static_cast<float>(segments);
                const float a1 = kPi * static_cast<float>(i + 1) /
                    static_cast<float>(segments);
                const auto makePoint = [=](float angle) {
                    const float radial = std::cos(angle) * radius;
                    const float vertical = side *
                        (cylinderHalfHeight +
                            std::sin(angle) * radius);
                    return radialAxis == 0
                        ? MATH::Vec3{ radial, vertical, 0.0f }
                        : MATH::Vec3{ 0.0f, vertical, radial };
                };
                SubmitLine(
                    matrix,
                    makePoint(a0),
                    makePoint(a1),
                    color,
                    selected);
            }
        }

        void DrawCollider(
            const GameObject& object,
            const ComponentGizmoDrawContext& context) {
            const bool selected =
                object.GetDocumentId() == context.selectedObjectId;
            MATH::Vec3 objectPosition{};
            MATH::Quat objectRotation{};
            MATH::Vec3 objectScale{};
            const MATH::Mat4 objectWorld =
                object.GetTransform().GetWorldMatrix();
            if (!MATH::DecomposeTRS(
                    objectWorld,
                    objectPosition,
                    objectRotation,
                    objectScale)) {
                return;
            }
            const MATH::Vec3 absoluteScale{
                std::abs(objectScale.x),
                std::abs(objectScale.y),
                std::abs(objectScale.z)
            };

            object.ForEachComponent<ColliderComponent>(
                [&](const ColliderComponent& collider) {
                    if (!collider.IsEnabled()) {
                        return;
                    }
                    if (collider.UsesCollisionGeometryAsset()) {
                        return;
                    }
                    const ResolvedColliderShape resolved =
                        collider.ResolveShape();
                    constexpr float kDegreesToRadians =
                        0.01745329251994329577f;
                    const MATH::Vec3 rotationDegrees =
                        resolved.rotationEulerDegrees;
                    const MATH::Quat localRotation =
                        MATH::Quat::FromEulerXYZ(
                            rotationDegrees.x * kDegreesToRadians,
                            rotationDegrees.y * kDegreesToRadians,
                            rotationDegrees.z * kDegreesToRadians);
                    const MATH::Mat4 authoredShape = objectWorld *
                        MATH::Mat4::Translate(resolved.center) *
                        MATH::Mat4::Rotate(localRotation);
                    MATH::Vec3 shapePosition{};
                    MATH::Quat shapeRotation{};
                    MATH::Vec3 ignoredScale{};
                    if (!MATH::DecomposeTRS(
                            authoredShape,
                            shapePosition,
                            shapeRotation,
                            ignoredScale)) {
                        return;
                    }
                    const MATH::Mat4 shapeWorld = MATH::Mat4::TRS(
                        shapePosition,
                        shapeRotation,
                        { 1.0f, 1.0f, 1.0f });
                    const unsigned int color = collider.IsTrigger()
                        ? kTriggerColor
                        : selected
                            ? kSelectedColliderColor
                            : kColliderColor;

                    if (resolved.type ==
                        PHYSICS::PhysicsShapeType::Box) {
                        const MATH::Vec3 size{
                            resolved.size.x * absoluteScale.x,
                            resolved.size.y * absoluteScale.y,
                            resolved.size.z * absoluteScale.z
                        };
                        RENDERER3D::DEBUG::WireCube cube{};
                        cube.transform.useExplicitMatrix = true;
                        cube.transform.explicitMatrix = shapeWorld *
                            MATH::Mat4::Scale(size);
                        cube.size = 1.0f;
                        cube.rgba = color;
                        cube.depthMode = selected
                            ? RENDERER3D::DEBUG::DebugDepthMode::XRay
                            : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;
                        RENDERER3D::DEBUG::SubmitWireCube(cube);
                        return;
                    }

                    const float radius = resolved.radius *
                        (std::max)(absoluteScale.x, absoluteScale.z);
                    if (resolved.type ==
                        PHYSICS::PhysicsShapeType::Sphere) {
                        SubmitCircle(
                            shapeWorld, 0, radius, 0.0f,
                            color, selected);
                        SubmitCircle(
                            shapeWorld, 1, radius, 0.0f,
                            color, selected);
                        SubmitCircle(
                            shapeWorld, 2, radius, 0.0f,
                            color, selected);
                        return;
                    }

                    const float totalHeight = (std::max)(
                        resolved.height * absoluteScale.y,
                        radius * 2.0f);
                    const float cylinderHalfHeight =
                        totalHeight * 0.5f - radius;
                    SubmitCircle(
                        shapeWorld, 1, radius,
                        cylinderHalfHeight, color, selected);
                    SubmitCircle(
                        shapeWorld, 1, radius,
                        -cylinderHalfHeight, color, selected);
                    for (const MATH::Vec3 radial :
                            std::array<MATH::Vec3, 4>{
                                MATH::Vec3{ radius, 0.0f, 0.0f },
                                MATH::Vec3{ -radius, 0.0f, 0.0f },
                                MATH::Vec3{ 0.0f, 0.0f, radius },
                                MATH::Vec3{ 0.0f, 0.0f, -radius } }) {
                        SubmitLine(
                            shapeWorld,
                            radial + MATH::Vec3{
                                0.0f, -cylinderHalfHeight, 0.0f },
                            radial + MATH::Vec3{
                                0.0f, cylinderHalfHeight, 0.0f },
                            color,
                            selected);
                    }
                    SubmitCapsuleArc(
                        shapeWorld, 0, radius,
                        cylinderHalfHeight, 1.0f,
                        color, selected);
                    SubmitCapsuleArc(
                        shapeWorld, 0, radius,
                        cylinderHalfHeight, -1.0f,
                        color, selected);
                    SubmitCapsuleArc(
                        shapeWorld, 1, radius,
                        cylinderHalfHeight, 1.0f,
                        color, selected);
                    SubmitCapsuleArc(
                        shapeWorld, 1, radius,
                        cylinderHalfHeight, -1.0f,
                        color, selected);
                });
        }
    }

    void RegisterColliderGizmoProvider(
        ComponentGizmoRegistry& registry) {
        (void)registry.Register(ComponentGizmoProvider{
            std::string(kColliderGizmoProviderId),
            "Colliders",
            true,
            DrawCollider
        });
    }

} // namespace HIKARI
