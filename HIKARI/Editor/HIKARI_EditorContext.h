#pragma once

namespace HIKARI {

    class GameObject;
    class ModelAsset;

    struct EditorSelection {
        GameObject* selectedObject = nullptr;
        ModelAsset* selectedAsset = nullptr;
    };

    struct DebugWindowState {
        bool showHierarchy = true;
        bool showInspector = true;
        bool showAssetBrowser = false;
        bool showStats = true;
        bool showEnvironment = true;
        bool showDebugCamera = true;
    };

} // namespace HIKARI
