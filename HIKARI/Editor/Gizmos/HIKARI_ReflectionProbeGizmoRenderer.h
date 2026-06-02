#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::EDITOR {

    class ReflectionProbeGizmoRenderer {
    public:
        void Submit(const SceneEnvironment& environment, bool drawDebugHelpers) const;
    };

} // namespace HIKARI::EDITOR
