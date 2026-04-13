#include "HIKARI_RuntimeSceneContext.h"

namespace HIKARI {

    SceneTransitionBus* RuntimeSceneContext::transitionBus_ = nullptr;

    void RuntimeSceneContext::SetTransitionBus(SceneTransitionBus* bus) {
        transitionBus_ = bus;
    }

    SceneTransitionBus* RuntimeSceneContext::GetTransitionBus() {
        return transitionBus_;
    }

} // namespace HIKARI
