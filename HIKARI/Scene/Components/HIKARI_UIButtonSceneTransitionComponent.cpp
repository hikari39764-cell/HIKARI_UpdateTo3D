#include "HIKARI_UIButtonSceneTransitionComponent.h"

#include <algorithm>

#include "Editor/HIKARI_IInspectorBuilder.h"
#include "HIKARI_Input.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    void UIButtonSceneTransitionComponent::Update(float dt) {
        (void)dt;

        if (!enabled_ || targetSceneId_.empty()) {
            return;
        }

        const bool isInside = IsMouseInsideRect();
        const bool clicked = requireLeftClick_
            ? HINPUT::IsMousePressed(HINPUT::MouseButton::Left)
            : HINPUT::IsMouseDown(HINPUT::MouseButton::Left);

        if (!isInside || !clicked) {
            return;
        }

        SceneTransitionBus* transitionBus = RuntimeSceneContext::GetTransitionBus();
        if (!transitionBus) {
            return;
        }

        SceneTransitionRequest request{};
        request.targetSceneId = targetSceneId_;
        request.targetSpawnPointId = targetSpawnPointId_;
        request.transitionProfileId = transitionProfileId_;
        request.useTransition = useTransition_;
        transitionBus->RequestTransition(request);
    }

    void UIButtonSceneTransitionComponent::RenderImGui() {
#if defined(_DEBUG)
        if (!debugDrawRect_) {
            return;
        }

        const ImU32 color = IM_COL32((debugColorRgba_ >> 24) & 0xFF, (debugColorRgba_ >> 16) & 0xFF, (debugColorRgba_ >> 8) & 0xFF, debugColorRgba_ & 0xFF);
        const ImVec2 minP{ screenRect_.x, screenRect_.y };
        const ImVec2 maxP{ screenRect_.x + screenRect_.w, screenRect_.y + screenRect_.h };
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRect(minP, maxP, color, 0.0f, 0, 2.0f);
        if (IsMouseInsideRect()) {
            drawList->AddRectFilled(minP, maxP, IM_COL32(255, 255, 80, 32));
        }
#endif
    }

    void UIButtonSceneTransitionComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["screenRect"] = { { "x", screenRect_.x }, { "y", screenRect_.y }, { "w", screenRect_.w }, { "h", screenRect_.h } };
        out["requireLeftClick"] = requireLeftClick_;
        out["targetSceneId"] = targetSceneId_;
        out["targetSpawnPointId"] = targetSpawnPointId_;
        out["transitionProfileId"] = transitionProfileId_;
        out["useTransition"] = useTransition_;
        out["debugDrawRect"] = debugDrawRect_;
        out["debugColorRgba"] = debugColorRgba_;
    }

    void UIButtonSceneTransitionComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        requireLeftClick_ = in.value("requireLeftClick", requireLeftClick_);
        targetSceneId_ = in.value("targetSceneId", targetSceneId_);
        targetSpawnPointId_ = in.value("targetSpawnPointId", targetSpawnPointId_);
        transitionProfileId_ = in.value("transitionProfileId", transitionProfileId_);
        useTransition_ = in.value("useTransition", useTransition_);
        debugDrawRect_ = in.value("debugDrawRect", debugDrawRect_);
        debugColorRgba_ = in.value("debugColorRgba", debugColorRgba_);

        if (in.contains("screenRect") && in["screenRect"].is_object()) {
            const nlohmann::json& rect = in["screenRect"];
            screenRect_.x = rect.value("x", screenRect_.x);
            screenRect_.y = rect.value("y", screenRect_.y);
            screenRect_.w = rect.value("w", screenRect_.w);
            screenRect_.h = rect.value("h", screenRect_.h);
        }
    }

    void UIButtonSceneTransitionComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.Vec2("Screen Pos", screenRect_.x, screenRect_.y);
        builder.Vec2("Screen Size", screenRect_.w, screenRect_.h);
        builder.Bool("Require Left Click", requireLeftClick_);
        builder.SceneIdPicker("Target Scene ID", targetSceneId_);
        builder.String("Target Spawn Point", targetSpawnPointId_);
        builder.String("Transition Profile", transitionProfileId_);
        builder.Bool("Use Transition", useTransition_);
        builder.Bool("Debug Draw Rect", debugDrawRect_);

        int colorInt = static_cast<int>(debugColorRgba_);
        if (builder.Int("Debug RGBA", colorInt)) {
            debugColorRgba_ = static_cast<uint32_t>((std::max)(0, colorInt));
        }
    }

    bool UIButtonSceneTransitionComponent::IsMouseInsideRect() const {
        const Vector2 mouse = HINPUT::GetMousePosition();
        return (mouse.x >= screenRect_.x) && (mouse.x <= (screenRect_.x + screenRect_.w))
            && (mouse.y >= screenRect_.y) && (mouse.y <= (screenRect_.y + screenRect_.h));
    }

} // namespace HIKARI
