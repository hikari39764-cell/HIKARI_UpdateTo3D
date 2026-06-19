#pragma once

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpaceGeometryAux.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"

namespace HIKARI {
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::SCREENSPACE {

    struct ScreenSpaceRuntimeState {
        ScreenSpaceGeometryAux geometryAux{};
        SsaoRenderer ssaoRenderer{};
        RENDER3D::TextureResourceHandle fallbackAoTextureResource{};
        int fallbackAoTextureHandle = -1;
        bool geometryValid = false;
        bool ssaoValid = false;
    };

    struct ScreenSpaceFrameResult {
        bool geometryAuxWritten = false;
        bool ssaoRendered = false;
        D3D12_GPU_DESCRIPTOR_HANDLE aoSrv{};
        RENDER3D::TextureResourceHandle fallbackAoTextureResource{};
        int fallbackAoTextureHandle = -1;
    };

    ScreenSpaceRuntimeState& GetScreenSpaceRuntimeState();
    void ReleaseScreenSpaceRuntimeState();

    bool EnsureScreenSpaceFallbacks(ScreenSpaceRuntimeState& state);

    ScreenSpaceFrameResult ExecuteScreenSpacePreLightingPasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& cameraCb,
        const SceneEnvironment& environment);

} // namespace HIKARI::RENDER3D::SCREENSPACE
