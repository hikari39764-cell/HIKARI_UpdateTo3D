#include "HIKARI_ReflectionProbeGizmoRenderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

namespace HIKARI::EDITOR {

    namespace {

        unsigned int ResolveProbeColor(unsigned int validColor) {
            const REFLECTION::ReflectionProbeRuntimeData& probe =
                REFLECTION::GetActiveProbe();

            if (!probe.enabled) {
                return 0x6A6F78AA;
            }
            if (!probe.valid) {
                return 0xFF8A4CFF;
            }
            return validColor;
        }

        bool HasUsableBoxSize(const MATH::Vec3& size) {
            return size.x > 0.001f && size.y > 0.001f && size.z > 0.001f;
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

        MATH::Vec3 BoxMin(const MATH::Vec3& center, const MATH::Vec3& size) {
            const MATH::Vec3 halfSize = size * 0.5f;
            return {
                center.x - halfSize.x,
                center.y - halfSize.y,
                center.z - halfSize.z
            };
        }

        MATH::Vec3 BoxMax(const MATH::Vec3& center, const MATH::Vec3& size) {
            const MATH::Vec3 halfSize = size * 0.5f;
            return {
                center.x + halfSize.x,
                center.y + halfSize.y,
                center.z + halfSize.z
            };
        }

        void SubmitBox(
            const MATH::Vec3& center,
            const MATH::Vec3& size,
            unsigned int color) {

            const MATH::Vec3 bmin = BoxMin(center, size);
            const MATH::Vec3 bmax = BoxMax(center, size);
            const MATH::Vec3 corners[8] = {
                { bmin.x, bmin.y, bmin.z },
                { bmax.x, bmin.y, bmin.z },
                { bmax.x, bmin.y, bmax.z },
                { bmin.x, bmin.y, bmax.z },
                { bmin.x, bmax.y, bmin.z },
                { bmax.x, bmax.y, bmin.z },
                { bmax.x, bmax.y, bmax.z },
                { bmin.x, bmax.y, bmax.z },
            };
            const int edges[12][2] = {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            };

            for (const auto& edge : edges) {
                RENDERER3D::DEBUG::SubmitLine3D({
                    corners[edge[0]],
                    corners[edge[1]],
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

        const ReflectionProbeSettings& probe = environment.reflectionProbe;
        const MATH::Vec3 center = probe.position;
        const float radius = (std::max)(0.01f, probe.radius);
        const unsigned int centerColor = ResolveProbeColor(0x61E6A8FF);
        const unsigned int influenceColor = HasUsableBoxSize(probe.influenceBoxSize)
            ? ResolveProbeColor(0x5EC7FFFF)
            : 0xFF8A4CFF;
        const unsigned int projectionColor = HasUsableBoxSize(probe.projectionBoxSize)
            ? ResolveProbeColor(0xFFD166FF)
            : 0xFF8A4CFF;

        // Probe の中心と authoring volume を editor overlay に描画する。
        SubmitCenterCross(center, radius, centerColor);
        if (probe.influenceShape == ReflectionProbeInfluenceShape::Box) {
            SubmitBox(probe.influenceBoxCenter, probe.influenceBoxSize, influenceColor);
        } else {
            SubmitCircle(center, radius, 0, influenceColor);
            SubmitCircle(center, radius, 1, influenceColor);
            SubmitCircle(center, radius, 2, influenceColor);
        }
        if (probe.projectionShape == ReflectionProbeProjectionShape::Box) {
            SubmitBox(probe.projectionBoxCenter, probe.projectionBoxSize, projectionColor);
        }
    }

} // namespace HIKARI::EDITOR
