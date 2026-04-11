#pragma once

#include "HIKARI_Camera3D.h"
#include "HIKARI_ModelManager.h"
#include "HIKARI_SceneEnvironment.h"

namespace HIKARI::SKYRENDERER {

    void Reset();
    void Render(const Camera3D& camera, const SkySettings& settings, ModelManager& modelManager);

} // namespace HIKARI::SKYRENDERER
