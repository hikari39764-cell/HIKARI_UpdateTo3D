#include "HIKARI_LightDebugDraw.h"
#include "Editor/HIKARI_DebugWindowState.h"
#include "HIKARI_Renderer3D_Debug.h"

namespace HIKARI::LIGHTDEBUGDRAW {

    void SubmitDirectionalLightArrow(const MATH::Vec3& directionalDir, const DebugWindowState& debugWindowState) {
        if (!debugWindowState.showLightDebug) {
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

        const MATH::Vec3 arrowDir{ -dir.x, -dir.y, -dir.z };

        const MATH::Vec3 origin{ 0.0f, 0.0f, 0.0f };
        const MATH::Vec3 head = origin + (arrowDir * kArrowLength);

        RENDERER3D::DEBUG::SubmitLine3D({ origin, head, kLightColor });

        MATH::Vec3 right = MATH::Cross(arrowDir, { 0.0f, 1.0f, 0.0f });
        if (MATH::Length(right) <= 1e-6f) {
            right = MATH::Cross(arrowDir, { 1.0f, 0.0f, 0.0f });
        }
        right = MATH::Normalize(right);

        const MATH::Vec3 up = MATH::Normalize(MATH::Cross(right, arrowDir));
        const MATH::Vec3 neck = head - (arrowDir * kHeadLength);

        RENDERER3D::DEBUG::SubmitLine3D({ head, neck + (right * kHeadHalfWidth), kLightColor });
        RENDERER3D::DEBUG::SubmitLine3D({ head, neck - (right * kHeadHalfWidth), kLightColor });
        RENDERER3D::DEBUG::SubmitLine3D({ head, neck + (up * kHeadHalfWidth), kLightColor });
        RENDERER3D::DEBUG::SubmitLine3D({ head, neck - (up * kHeadHalfWidth), kLightColor });

        RENDERER3D::DEBUG::WireCube marker{};
        marker.transform.position = head;
        marker.size = kMarkerSize;
        marker.rgba = kLightColor;
        RENDERER3D::DEBUG::SubmitWireCube(marker);
    }

} // namespace HIKARI::LIGHTDEBUGDRAW
