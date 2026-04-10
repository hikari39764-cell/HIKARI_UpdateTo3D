#pragma once

namespace HIKARI {

    class IScene {
    public:
        virtual ~IScene() = default;

        virtual void OnEnter() {}
        virtual void OnExit() {}

        virtual void Update(float dt) = 0;
        virtual void Render() = 0;
        virtual void RenderImGui() = 0;

        virtual const char* GetSceneName() const = 0;
    };

} // namespace HIKARI
