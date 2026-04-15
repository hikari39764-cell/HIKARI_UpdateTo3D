#include "HIKARI_ComponentGizmoRenderer.h"

#include "Render3D/HIKARI_Renderer3D_Debug.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Components/HIKARI_DoorTransitionComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/HIKARI_TriggerVolumeComponent.h"
#include "Scene/Components/HIKARI_UIButtonSceneTransitionComponent.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {
    namespace {
        constexpr unsigned int kTriggerColor = 0x00FFFFFF;
        constexpr unsigned int kDoorTriggerColor = 0xFF66CCFF;
        constexpr unsigned int kSpawnColor = 0x55FF66FF;
        constexpr unsigned int kDoorArrowColor = 0xFF55DDFF;
        constexpr unsigned int kUiRectColor = 0xFFAA33FF;

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
    }

    void ComponentGizmoRenderer::SubmitWorldGizmos(const World& world, const ComponentGizmoState& state, SceneObjectId selectedObjectId) const {
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

            const TriggerVolumeComponent* trigger = object->GetComponent<TriggerVolumeComponent>();
            const DoorTransitionComponent* door = object->GetComponent<DoorTransitionComponent>();
            if (trigger && trigger->IsEnabled() && state.showTriggerVolumes) {
                RENDERER3D::DEBUG::WireCube cube{};
                cube.transform = transform;
                cube.transform.scale = { trigger->GetBoxSizeX(), trigger->GetBoxSizeY(), trigger->GetBoxSizeZ() };
                cube.size = 1.0f;
                cube.rgba = (door != nullptr) ? kDoorTriggerColor : kTriggerColor;
                RENDERER3D::DEBUG::SubmitWireCube(cube);
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

            if (door && door->IsEnabled() && state.showDoorTransitions) {
                const MATH::Mat4 worldMtx = transform.GetWorldMatrix();
                const MATH::Vec4 from4 = worldMtx.TransformPoint({ 0.0f, 1.0f, 0.0f, 1.0f });
                const MATH::Vec4 to4 = worldMtx.TransformPoint({ 0.0f, 1.0f, 1.1f, 1.0f });
                SubmitArrow(MATH::Vec3{ from4.x, from4.y, from4.z }, MATH::Vec3{ to4.x, to4.y, to4.z }, kDoorArrowColor);
            }
        }
    }

    void ComponentGizmoRenderer::DrawScreenSpaceGizmos(const World& world, const ComponentGizmoState& state, SceneObjectId selectedObjectId) const {
#if defined(_DEBUG)
        if (!state.showComponentGizmos || !state.showUIScreenRects) {
            return;
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        for (const auto& object : world.GetObjects()) {
            if (!object) {
                continue;
            }
            if (!ShouldDrawForObject(*object, state, selectedObjectId)) {
                continue;
            }

            const auto* button = object->GetComponent<UIButtonSceneTransitionComponent>();
            if (!button || !button->IsEnabled() || !button->IsDebugDrawRectEnabled()) {
                continue;
            }

            const UIButtonSceneTransitionComponent::ScreenRect& rect = button->GetScreenRect();
            const unsigned int colorRgba = button->GetDebugColorRgba() == 0 ? kUiRectColor : button->GetDebugColorRgba();
            const ImU32 color = IM_COL32((colorRgba >> 24) & 0xFF, (colorRgba >> 16) & 0xFF, (colorRgba >> 8) & 0xFF, colorRgba & 0xFF);
            const ImVec2 minP{ rect.x, rect.y };
            const ImVec2 maxP{ rect.x + rect.w, rect.y + rect.h };
            drawList->AddRect(minP, maxP, color, 0.0f, 0, 2.0f);

            if (!button->GetTargetSceneId().empty()) {
                drawList->AddText(ImVec2(minP.x, minP.y - 16.0f), color, button->GetTargetSceneId().c_str());
            }
        }
#else
        (void)world;
        (void)state;
        (void)selectedObjectId;
#endif
    }

} // namespace HIKARI
