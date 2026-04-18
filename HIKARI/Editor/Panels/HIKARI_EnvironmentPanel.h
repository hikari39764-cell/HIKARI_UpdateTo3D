#pragma once

namespace HIKARI {

    struct SceneEnvironment;

    namespace SKYRENDERER {
        struct SkyRendererDebugState;
    }

    class EnvironmentPanel {
    public:
        void Draw(SceneEnvironment& environment, const SKYRENDERER::SkyRendererDebugState* skyDebugState) const;
    };

} // namespace HIKARI
