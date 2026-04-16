#pragma once

#include <string>

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class UIButtonSceneTransitionComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "UIButtonSceneTransitionComponent"; }

        struct ScreenRect {
            float x = 0.0f;
            float y = 0.0f;
            float w = 200.0f;
            float h = 80.0f;
        };

        void Update(float dt) override;
        void RenderImGui() override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const;
        bool IsDebugDrawRectEnabled() const;
        const ScreenRect& GetScreenRect() const;
        uint32_t GetDebugColorRgba() const;
        const std::string& GetTargetSceneId() const;

    private:
        bool IsMouseInsideRect() const;

    private:
        bool enabled_ = true;
        ScreenRect screenRect_{};
        bool requireLeftClick_ = true;
        std::string targetSceneId_{ "Title" };
        std::string transitionProfileId_{ "DefaultFade" };
        bool useTransition_ = true;
        bool debugDrawRect_ = true;
        uint32_t debugColorRgba_ = 0xFFCC33FF;
    };

} // namespace HIKARI
