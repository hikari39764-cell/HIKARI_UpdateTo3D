#include "HIKARI_LightProbeVolumeGizmoRenderer.h"

#include <algorithm>
#include <initializer_list>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"

namespace HIKARI::EDITOR {

    namespace {

        unsigned int ResolveVolumeColor(const LightProbeVolumeSettings& settings) {
            const RENDER3D::LIGHTPROBE::LightProbeVolumeRuntimeData& runtime =
                RENDER3D::LIGHTPROBE::GetRuntimeData();
            const RENDER3D::LIGHTING::SceneLightingRuntimeData& lighting =
                RENDER3D::LIGHTING::GetLastLightingRuntimeData();
            const bool runtimeMatchesAuthoring =
                runtime.countX == settings.countX &&
                runtime.countY == settings.countY &&
                runtime.countZ == settings.countZ &&
                runtime.origin.x == settings.origin.x &&
                runtime.origin.y == settings.origin.y &&
                runtime.origin.z == settings.origin.z &&
                runtime.size.x == settings.size.x &&
                runtime.size.y == settings.size.y &&
                runtime.size.z == settings.size.z;

            if (!settings.enabled) {
                return 0x6A6F78AA;
            }
            if (runtime.valid &&
                runtimeMatchesAuthoring &&
                lighting.source == RENDER3D::LIGHTING::LightingRuntimeSource::BakedRuntime) {
                return 0x61E6A8FF;
            }
            if (lighting.lightProbeVolumeLoaded || runtime.probeCount > 0u) {
                return 0xFF4C5CFF;
            }
            return 0xFFD166FF;
        }

        void SubmitLine(const MATH::Vec3& from, const MATH::Vec3& to, unsigned int color) {
            RENDERER3D::DEBUG::SubmitLine3D({
                from,
                to,
                color,
                RENDERER3D::DEBUG::DebugDepthMode::XRay
            });
        }

        void SubmitBox(const MATH::Vec3& origin, const MATH::Vec3& size, unsigned int color) {
            const MATH::Vec3 p000 = origin;
            const MATH::Vec3 p100{ origin.x + size.x, origin.y, origin.z };
            const MATH::Vec3 p010{ origin.x, origin.y + size.y, origin.z };
            const MATH::Vec3 p110{ origin.x + size.x, origin.y + size.y, origin.z };
            const MATH::Vec3 p001{ origin.x, origin.y, origin.z + size.z };
            const MATH::Vec3 p101{ origin.x + size.x, origin.y, origin.z + size.z };
            const MATH::Vec3 p011{ origin.x, origin.y + size.y, origin.z + size.z };
            const MATH::Vec3 p111{ origin.x + size.x, origin.y + size.y, origin.z + size.z };

            SubmitLine(p000, p100, color);
            SubmitLine(p100, p110, color);
            SubmitLine(p110, p010, color);
            SubmitLine(p010, p000, color);
            SubmitLine(p001, p101, color);
            SubmitLine(p101, p111, color);
            SubmitLine(p111, p011, color);
            SubmitLine(p011, p001, color);
            SubmitLine(p000, p001, color);
            SubmitLine(p100, p101, color);
            SubmitLine(p110, p111, color);
            SubmitLine(p010, p011, color);
        }

        void SubmitProbeCross(
            const MATH::Vec3& position,
            const MATH::Vec3& spacing,
            unsigned int color) {

            const float sx = (std::max)(0.05f, spacing.x);
            const float sy = (std::max)(0.05f, spacing.y);
            const float sz = (std::max)(0.05f, spacing.z);
            const float minSpacing = (std::min)((std::min)(sx, sy), sz);
            const float size = (std::clamp)(minSpacing * 0.08f, 0.06f, 0.25f);

            SubmitLine(
                { position.x - size, position.y, position.z },
                { position.x + size, position.y, position.z },
                color);
            SubmitLine(
                { position.x, position.y - size, position.z },
                { position.x, position.y + size, position.z },
                color);
            SubmitLine(
                { position.x, position.y, position.z - size },
                { position.x, position.y, position.z + size },
                color);
        }

    } // namespace

    void LightProbeVolumeGizmoRenderer::Submit(
        const LightProbeVolumeSettings& sourceSettings,
        bool drawDebugHelpers) const {

        if (!drawDebugHelpers) {
            return;
        }

        LightProbeVolumeSettings settings = sourceSettings;
        ClampLightProbeVolumeSettings(settings);
        const unsigned int color = ResolveVolumeColor(settings);
        const MATH::Vec3 spacing = GetLightProbeVolumeSpacing(settings);

        // Volume の範囲と probe 配置を editor overlay として描画する。
        SubmitBox(settings.origin, settings.size, color);

        for (uint32_t z = 0; z < settings.countZ; ++z) {
            for (uint32_t y = 0; y < settings.countY; ++y) {
                for (uint32_t x = 0; x < settings.countX; ++x) {
                    SubmitProbeCross(
                        GetLightProbeVolumeProbePosition(settings, x, y, z),
                        spacing,
                        color);
                }
            }
        }
    }

} // namespace HIKARI::EDITOR
