#pragma once

#include <string_view>
#include <vector>

#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"

namespace HIKARI::RENDER3D::RUNTIME {

    std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(const RenderModelAsset& renderModel);

    std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(
        const RenderModelAsset& renderModel,
        const ModelAsset* model,
        std::string_view animationClipName,
        float animationTimeSec,
        bool animationLoop);

    bool ResolveRenderSurfaceDrawWorldMatrix(
        const Transform3D& objectWorldTransform,
        const RenderSurfaceRecord& surface,
        const std::vector<MATH::Mat4>& nodeGlobals,
        MATH::Mat4& outDrawWorldMatrix);

    Bounds ResolveRenderSurfaceWorldBounds(
        const Bounds& fallbackWorldBounds,
        const RenderSurfaceRecord& surface,
        const MATH::Mat4& drawWorldMatrix,
        bool hasDrawWorldMatrix);

    bool HasValidRenderSurfacePrimitive(
        const ModelAsset* model,
        const RenderSurfaceRecord& surface);

} // namespace HIKARI::RENDER3D::RUNTIME
