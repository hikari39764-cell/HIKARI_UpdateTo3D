#pragma once

#include <cstdint>
#include <optional>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    enum class EditorWorkspaceId : uint8_t {
        Scene,
        Cinematics,
        ModelCollision,
    };

    struct EditorWorkspaceOpenRequest {
        EditorWorkspaceId workspaceId = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
        std::optional<AssetGuid> sequenceAssetGuid{};
        std::optional<AssetGuid> modelAssetGuid{};
    };

    struct EditorWorkspaceActivation {
        EditorWorkspaceId previous = EditorWorkspaceId::Scene;
        EditorWorkspaceId current = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
        std::optional<AssetGuid> sequenceAssetGuid{};
        std::optional<AssetGuid> modelAssetGuid{};
    };

} // namespace HIKARI::EDITOR
