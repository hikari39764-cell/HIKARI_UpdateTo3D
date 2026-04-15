#pragma once

#include <cstdint>
#include <string>

namespace HIKARI {

    class GameObject;
    class ModelAsset;

    struct EditorSelection {
        GameObject* selectedObject = nullptr;
        ModelAsset* selectedAsset = nullptr;
    };

    struct ComponentGizmoState {
        bool showComponentGizmos = true;
        bool showTriggerVolumes = true;
        bool showSpawnPoints = true;
        bool showDoorTransitions = true;
        bool showUIScreenRects = true;
        bool showOnlySelectedObject = false;
    };

    struct AuthoringWindowState {
        bool showSceneDocument = true;
        bool showHierarchy = true;
        bool showInspector = true;
    };

    struct ResourceWindowState {
        bool showAssetBrowser = false;
        bool showEnvironment = true;
    };

    struct RuntimeDebugWindowState {
        bool showStats = true;
        bool showDebugCamera = true;
        bool showGizmoSettings = true;
    };

    struct DebugWindowState {
        AuthoringWindowState authoring{};
        ResourceWindowState resources{};
        RuntimeDebugWindowState runtime{};
    };

    struct EditorContext {
        DebugWindowState windows{};
        ComponentGizmoState gizmos{};
        EditorSelection selection{};

        std::string sceneNameEditBuffer{ "Untitled" };
        std::string saveAsNameBuffer{ "Untitled" };
        std::string prefabNameBuffer{ "NewPrefab" };
        std::string componentAddStatusMessage{};
        bool componentAddStatusIsError = false;
        uint64_t nextSceneObjectId = 1;
        bool sceneDirty = false;
    };

} // namespace HIKARI
