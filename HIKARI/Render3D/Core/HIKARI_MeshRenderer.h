#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <DirectXMath.h>
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::MESHRENDERER {

    void Reset();
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[4], bool materialFxValuesInitialized);
    void SubmitStaticMeshMatrix(const ModelAsset& asset, const MATH::Mat4& worldMatrix, const MATH::Mat4& normalMatrix, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[4], bool materialFxValuesInitialized);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);

} // namespace HIKARI::MESHRENDERER
