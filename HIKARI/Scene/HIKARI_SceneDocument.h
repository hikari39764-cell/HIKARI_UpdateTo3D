#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "Render3D/HIKARI_SceneEnvironment.h"

namespace HIKARI {

    struct SceneObjectId {
        uint64_t value = 0;

        bool operator==(const SceneObjectId& rhs) const {
            return value == rhs.value;
        }
    };

    struct TransformData {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 rotationEulerDeg{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct SceneComponentData {
        std::string type{};
        nlohmann::json properties = nlohmann::json::object();
    };

    struct SceneObjectData {
        SceneObjectId id{};
        std::string name{};
        std::string sourcePrefabId{};
        std::optional<SceneObjectId> parent{};
        TransformData transform{};
        std::vector<SceneComponentData> components{};
        bool editorVisible = true;
        bool enabled = true;
    };

    struct SceneDocument {
        uint32_t version = 1;
        std::string sceneName = "Untitled";
        SceneEnvironment environment{};
        std::vector<SceneObjectData> objects{};
    };

} // namespace HIKARI
