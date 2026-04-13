#pragma once

namespace HIKARI {

    class SceneTransitionBus;

    class RuntimeSceneContext {
    public:
        static void SetTransitionBus(SceneTransitionBus* bus);
        static SceneTransitionBus* GetTransitionBus();

    private:
        static SceneTransitionBus* transitionBus_;
    };

} // namespace HIKARI
