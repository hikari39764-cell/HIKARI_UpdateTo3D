#pragma once

#include <cstdint>
#include <string>

#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"

namespace HIKARI {

    class GameObject;
    class ModelAsset;

    struct EditorSelection {
        GameObject* selectedObject = nullptr;
        ModelAsset* selectedAsset = nullptr;
        std::string selectedAssetGuid{};
        std::string selectedAssetPath{};
    };

    struct ComponentGizmoState {
        bool showComponentGizmos = true;
        bool showTriggerVolumes = false;
        bool showSpawnPoints = false;
        bool showDoorTransitions = false;
        bool showUIScreenRects = false;
        bool showOnlySelectedObject = false;
    };

    struct ViewportOverlayState {
        bool showGrid = true;
        bool showAxis = true;
    };

    struct ViewportWindowState {
        bool showGameView = true;
        bool showViewportHud = true;
        bool gameOnlyMode = false;
        float gameViewResolutionScale = 1.0f;
    };

    struct AuthoringWindowState {
        bool showSceneWorkspace = true;
    };

    struct ResourceWindowState {
        bool showAssetBrowser = true;
        bool showEnvironment = true;
    };

    struct RuntimeDebugWindowState {
        bool showDebugWorkspace = true;
    };

    struct DebugWindowState {
        ViewportWindowState viewport{};
        AuthoringWindowState authoring{};
        ResourceWindowState resources{};
        RuntimeDebugWindowState runtime{};
    };

    struct EditorContext {
        DebugWindowState windows{};
        ComponentGizmoState gizmos{};
        ViewportOverlayState overlays{};
        EditorSelection selection{};

        std::string sceneNameEditBuffer{ "Untitled" };
        std::string saveAsNameBuffer{ "Untitled" };
        bool saveAsNameOverriddenByUser = false;
        std::string prefabNameBuffer{ "NewPrefab" };
        std::string componentAddStatusMessage{};
        bool componentAddStatusIsError = false;
        uint64_t nextSceneObjectId = 1;
        bool sceneDirty = false;
        RuntimeResourceRefreshReport lastRuntimeRefreshReport{};
    };

} // namespace HIKARI
