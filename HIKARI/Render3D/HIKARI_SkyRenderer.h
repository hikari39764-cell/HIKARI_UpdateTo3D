#pragma once

#include <string>

#include "HIKARI_Camera3D.h"
#include "HIKARI_ModelManager.h"
#include "HIKARI_SceneEnvironment.h"
#include "HIKARI_SkyManager.h"

namespace HIKARI::SKYRENDERER {

    struct SkyRendererDebugState {
        bool initialized = false;
        bool lastRenderSubmitted = false;
        bool skyAssetFound = false;
        bool skyMeshLoaded = false;
        bool skyMeshValid = false;
        bool textureValid = false;
        std::string activeSkyAsset{};
        std::string activeTexturePath{};
    };

    void Reset();
    void Render(const Camera3D& camera, const SkySettings& settings, ModelManager& modelManager, SkyManager& skyManager);
    const SkyRendererDebugState& GetDebugState();

} // namespace HIKARI::SKYRENDERER
