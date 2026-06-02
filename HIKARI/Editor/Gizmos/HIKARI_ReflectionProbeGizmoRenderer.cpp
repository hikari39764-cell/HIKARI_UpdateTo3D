#include "HIKARI_ReflectionProbeGizmoRenderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

namespace HIKARI::EDITOR {

    namespace {

        unsigned int ResolveProbeColor() {
            const RENDER3D::LIGHTING::SceneLightingRuntimeData& lighting =
                RENDER3D::LIGHTING::GetLastLightingRuntimeData();
            const REFLECTION::ReflectionProbeRuntimeData& probe =
                REFLECTION::GetActiveProbe();

            if (!probe.enabled) {
                return 0x6A6F78AA;
            }
            if (!probe.valid) {
                return 0xFF8A4CFF;
            }
            if (lighting.source == RENDER3D::LIGHTING::LightingRuntimeSource::BakedRuntime) {
                return 0x61E6A8FF;
            }
            return 0x5EC7FFFF;
        }

        MATH::Vec3 CirclePoint(
            const MATH::Vec3& center,
            float radius,
            int axis,
            float t) {

            const float c = std::cos(t) * radius;
            const float s = std::sin(t) * radius;
            if (axis == 0) {
                return { center.x, center.y + c, center.z + s };
            }
            if (axis == 1) {
                return { center.x + c, center.y, center.z + s };
            }
            return { center.x + c, center.y + s, center.z };
        }

        void SubmitCircle(
            const MATH::Vec3& center,
            float radius,
            int axis,
            unsigned int color) {

            constexpr int kSegments = 48;
            for (int i = 0; i < kSegments; ++i) {
                const float t0 = (static_cast<float>(i) / static_cast<float>(kSegments)) *
                    (2.0f * std::numbers::pi_v<float>);
                const float t1 = (static_cast<float>(i + 1) / static_cast<float>(kSegments)) *
                    (2.0f * std::numbers::pi_v<float>);
                RENDERER3D::DEBUG::SubmitLine3D({
                    CirclePoint(center, radius, axis, t0),
                    CirclePoint(center, radius, axis, t1),
                    color,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
            }
        }

        void SubmitCenterCross(
            const MATH::Vec3& center,
            float radius,
            unsigned int color) {

            const float size = (std::clamp)(radius * 0.08f, 0.12f, 0.5f);
            RENDERER3D::DEBUG::SubmitLine3D({
                { center.x - size, center.y, center.z },
                { center.x + size, center.y, center.z },
                color,
                RENDERER3D::DEBUG::DebugDepthMode::XRay
            });
            RENDERER3D::DEBUG::SubmitLine3D({
                { center.x, center.y - size, center.z },
                { center.x, center.y + size, center.z },
                color,
                RENDERER3D::DEBUG::DebugDepthMode::XRay
            });
            RENDERER3D::DEBUG::SubmitLine3D({
                { center.x, center.y, center.z - size },
                { center.x, center.y, center.z + size },
                color,
                RENDERER3D::DEBUG::DebugDepthMode::XRay
            });
        }

    } // namespace

    void ReflectionProbeGizmoRenderer::Submit(
        const SceneEnvironment& environment,
        bool drawDebugHelpers) const {

        if (!drawDebugHelpers || !environment.reflectionProbe.enabled) {
            return;
        }

        const MATH::Vec3 center = environment.reflectionProbe.position;
        const float radius = (std::max)(0.01f, environment.reflectionProbe.radius);
        const unsigned int color = ResolveProbeColor();

        // Probe 範囲を editor overlay に描画する。
        SubmitCenterCross(center, radius, color);
        SubmitCircle(center, radius, 0, color);
        SubmitCircle(center, radius, 1, color);
        SubmitCircle(center, radius, 2, color);
    }

} // namespace HIKARI::EDITOR
