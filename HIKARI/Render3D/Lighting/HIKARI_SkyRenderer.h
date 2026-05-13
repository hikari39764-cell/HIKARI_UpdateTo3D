#pragma once

#include <string>

#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelManager.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_SkyManager.h"

namespace HIKARI::SKYRENDERER {

    struct SkyRendererDebugState {
        bool initialized = false;
        bool lastRenderSubmitted = false;
        bool skyAssetFound = false;
        bool cubemapLoaded = false;
        bool usingFallback = false;
        bool textureValid = false;
        SkyMode mode = SkyMode::None;
        int cubemapHandle = -1;
        int textureHandle = -1;
        size_t psoCreateCount = 0;
        size_t drawCount = 0;
        std::string activeSkyAsset{};
        std::string activeTexturePath{};
    };

    void Reset();
    void Render(const Camera3D& camera, const SkySettings& settings, ModelManager& modelManager, SkyManager& skyManager);
    const SkyRendererDebugState& GetDebugState();

} // namespace HIKARI::SKYRENDERER
