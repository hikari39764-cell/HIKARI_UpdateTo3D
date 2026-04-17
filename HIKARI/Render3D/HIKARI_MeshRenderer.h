#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "HIKARI_Camera3D.h"
#include "HIKARI_ModelAsset.h"
#include "HIKARI_SceneEnvironment.h"
#include "HIKARI_Transform3D.h"

namespace HIKARI::MESHRENDERER {

    void Reset();
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);

} // namespace HIKARI::MESHRENDERER
