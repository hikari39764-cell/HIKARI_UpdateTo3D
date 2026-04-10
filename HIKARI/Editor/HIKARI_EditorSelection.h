#pragma once

namespace HIKARI {

    class GameObject;
    class ModelAsset;

    struct EditorSelection {
        GameObject* selectedObject = nullptr;
        ModelAsset* selectedAsset = nullptr;
    };

} // namespace HIKARI
