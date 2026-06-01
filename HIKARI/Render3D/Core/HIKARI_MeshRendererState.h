#pragma once

#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/ScreenSpace/HIKARI_SceneGeometryBuffer.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"

namespace HIKARI::MESHRENDERER {

    struct MeshRendererState {
        bool initialized = false;

        MeshPipelineStore pipelines;

        Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> skyEnvironmentCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> jointPaletteCB;

        CameraCB* cameraMapped = nullptr;
        ObjectCB* objectMapped = nullptr;
        LightCB* lightMapped = nullptr;
        ShadowCB* shadowMapped = nullptr;
        SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;

        std::vector<DrawItem> drawItems;
        MeshRendererDebugStats debugStats;

        int fallbackTextureHandle = -1;
        int fallbackNormalTextureHandle = -1;
        int fallbackBlackTextureHandle = -1;
        int fallbackCubeTextureHandle = -1;
        int fallbackAoTextureHandle = -1;

        MeshPrimitiveCache primitiveCache;
        MeshMaterialResolver materialResolver;
        RENDER3D::SCREENSPACE::SceneGeometryBuffer geometryBuffer;
        RENDER3D::SCREENSPACE::SsaoRenderer ssaoRenderer;

        float elapsedTimeSec = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
