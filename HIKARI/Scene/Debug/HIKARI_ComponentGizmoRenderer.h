#pragma once

#include "Editor/HIKARI_EditorContext.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    class World;

    class ComponentGizmoRenderer {
    public:
        void SubmitWorldGizmos(const World& world, const ComponentGizmoState& state, SceneObjectId selectedObjectId) const;
        void DrawScreenSpaceGizmos(const World& world, const ComponentGizmoState& state, SceneObjectId selectedObjectId) const;
    };

} // namespace HIKARI
