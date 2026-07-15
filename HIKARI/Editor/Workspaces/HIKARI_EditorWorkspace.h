#pragma once

#include <cstdint>
#include <optional>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    enum class EditorWorkspaceId : uint8_t {
        Scene,
        Cinematics,
    };

    struct EditorWorkspaceOpenRequest {
        EditorWorkspaceId workspaceId = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
    };

    struct EditorWorkspaceActivation {
        EditorWorkspaceId previous = EditorWorkspaceId::Scene;
        EditorWorkspaceId current = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
    };

} // namespace HIKARI::EDITOR
