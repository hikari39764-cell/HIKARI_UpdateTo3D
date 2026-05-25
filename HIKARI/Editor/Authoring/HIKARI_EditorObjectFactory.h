#pragma once

#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class DocumentSceneBase;
    class GameObject;

namespace EDITOR {

    struct CreateObjectRequest {
        std::string name{};
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 rotation{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    GameObject* CreateEmptyObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request);

    GameObject* CreateModelObject(
        DocumentSceneBase& scene,
        const AssetGuid& modelGuid,
        const CreateObjectRequest& request);

} // namespace EDITOR
} // namespace HIKARI
