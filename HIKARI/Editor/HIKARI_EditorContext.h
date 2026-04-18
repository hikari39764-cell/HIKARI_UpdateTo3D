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
        bool showSceneObject = true;
    };

    struct ResourceWindowState {
        bool showAssetBrowser = false;
        bool showEnvironment = true;
    };

    struct RuntimeDebugWindowState {
        bool showStats = true;
        bool showDebugTools = true;
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
        bool saveAsNameOverriddenByUser = false;
        std::string prefabNameBuffer{ "NewPrefab" };
        std::string componentAddStatusMessage{};
        bool componentAddStatusIsError = false;
        uint64_t nextSceneObjectId = 1;
        bool sceneDirty = false;
    };

} // namespace HIKARI
