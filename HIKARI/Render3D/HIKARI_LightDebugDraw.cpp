#include "HIKARI_LightDebugDraw.h"

#include <cmath>
#include <numbers>
#include "HIKARI_Renderer3D_Debug.h"
#include "Render3D/HIKARI_SceneEnvironment.h"

namespace HIKARI::LIGHTDEBUGDRAW {

    void SubmitDirectionalLightArrow(const MATH::Vec3& directionalDir, const SceneEnvironment& environment) {
        if (!environment.showLightDebug || !environment.directional.enabled) {
            return;
        }

        constexpr float kArrowLength = 2.5f;
        constexpr float kHeadLength = 0.35f;
        constexpr float kHeadHalfWidth = 0.12f;
        constexpr float kMarkerSize = 0.12f;
        constexpr unsigned int kLightColor = 0xFFF2A3FF;

        MATH::Vec3 dir = MATH::Normalize(directionalDir);
        if (MATH::Length(dir) <= 1e-6f) {
            dir = { 0.0f, -1.0f, 0.0f };
        }

        const MATH::Vec3 origin{ 0.0f, 1.5f, 0.0f };
        const MATH::Vec3 head = origin + (dir * kArrowLength);

        RENDERER3D::DEBUG::SubmitLine3D({ origin, head, kLightColor });

        MATH::Vec3 right = MATH::Cross(dir, { 0.0f, 1.0f, 0.0f });
        if (MATH::Length(right) <= 1e-6f) {
            right = MATH::Cross(dir, { 1.0f, 0.0f, 0.0f });
        }
        right = MATH::Normalize(right);

        const MATH::Vec3 up = MATH::Normalize(MATH::Cross(right, dir));
        const MATH::Vec3 neck = head - (dir * kHeadLength);

        RENDERER3D::DEBUG::SubmitLine3D({ head, neck + (right * kHeadHalfWidth), kLightColor });
        RENDERER3D::DEBUG::SubmitLine3D({ head, neck - (right * kHeadHalfWidth), kLightColor });
        RENDERER3D::DEBUG::SubmitLine3D({ head, neck + (up * kHeadHalfWidth), kLightColor });
        RENDERER3D::DEBUG::SubmitLine3D({ head, neck - (up * kHeadHalfWidth), kLightColor });

        RENDERER3D::DEBUG::WireCube marker{};
        marker.transform.position = origin;
        marker.size = kMarkerSize;
        marker.rgba = kLightColor;
        RENDERER3D::DEBUG::SubmitWireCube(marker);
    }

    void SubmitPointLightDebug(const SceneEnvironment& environment) {
        if (!environment.showPointLightMarkers) {
            return;
        }

        constexpr unsigned int kPointLightColor = 0xFFC18BFF;
        for (const PointLight& light : environment.pointLights) {
            if (!light.enabled) {
                continue;
            }

            RENDERER3D::DEBUG::WireCube marker{};
            marker.transform.position = light.position;
            marker.size = 0.12f;
            marker.rgba = kPointLightColor;
            RENDERER3D::DEBUG::SubmitWireCube(marker);

            constexpr int kSegments = 24;
            constexpr unsigned int kRangeColor = 0x66C18BFF;
            for (int axis = 0; axis < 3; ++axis) {
                for (int i = 0; i < kSegments; ++i) {
                    const float t0 = (static_cast<float>(i) / static_cast<float>(kSegments)) * (2.0f * std::numbers::pi_v<float>);
                    const float t1 = (static_cast<float>(i + 1) / static_cast<float>(kSegments)) * (2.0f * std::numbers::pi_v<float>);
                    MATH::Vec3 p0{};
                    MATH::Vec3 p1{};
                    if (axis == 0) {
                        p0 = { light.position.x, light.position.y + std::cos(t0) * light.range, light.position.z + std::sin(t0) * light.range };
                        p1 = { light.position.x, light.position.y + std::cos(t1) * light.range, light.position.z + std::sin(t1) * light.range };
                    } else if (axis == 1) {
                        p0 = { light.position.x + std::cos(t0) * light.range, light.position.y, light.position.z + std::sin(t0) * light.range };
                        p1 = { light.position.x + std::cos(t1) * light.range, light.position.y, light.position.z + std::sin(t1) * light.range };
                    } else {
                        p0 = { light.position.x + std::cos(t0) * light.range, light.position.y + std::sin(t0) * light.range, light.position.z };
                        p1 = { light.position.x + std::cos(t1) * light.range, light.position.y + std::sin(t1) * light.range, light.position.z };
                    }
                    RENDERER3D::DEBUG::SubmitLine3D({ p0, p1, kRangeColor });
                }
            }
        }
    }

} // namespace HIKARI::LIGHTDEBUGDRAW
