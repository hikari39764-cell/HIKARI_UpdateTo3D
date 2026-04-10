#pragma once
#include <vector>
#include "HIKARI_Camera3D.h"
#include "HIKARI_ModelAsset.h"
#include "HIKARI_SceneLighting.h"
#include "HIKARI_Transform3D.h"

namespace HIKARI::MESHRENDERER {

    void Reset();
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform);
    void RenderAll(const Camera3D& camera, const SceneLighting& lighting);

} // namespace HIKARI::MESHRENDERER
