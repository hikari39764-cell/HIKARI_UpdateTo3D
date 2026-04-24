#pragma once

#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "HIKARI_ModelRenderItem.h"

namespace HIKARI::MODELRENDERER {

    void Reset();
    void SubmitModel(const ModelRenderItem& item);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);

}
