#pragma once

#include <vector>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    inline std::vector<SceneSystemData> CreateDefaultSceneSystems() {
        return {
            SceneSystemData{ "ModelRenderSystem", true, 100, nlohmann::json::object() },
            SceneSystemData{ "PlayerMovementSystem", true, 140, nlohmann::json::object() },
            SceneSystemData{ "AnimationSystem", true, 150, nlohmann::json::object() },
            SceneSystemData{ "SceneScanFxSystem", true, 180, nlohmann::json::object() },
            SceneSystemData{ "CameraFollowSystem", true, 190, nlohmann::json::object() },
        };
    }

} // namespace HIKARI
