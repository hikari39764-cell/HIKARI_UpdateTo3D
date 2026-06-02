#pragma once

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    class LightProbeVolumeGizmoRenderer {
    public:
        void Submit(
            const LightProbeVolumeSettings& settings,
            bool drawDebugHelpers) const;
    };

} // namespace HIKARI::EDITOR
