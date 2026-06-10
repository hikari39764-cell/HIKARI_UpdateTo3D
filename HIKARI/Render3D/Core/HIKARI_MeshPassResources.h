#pragma once

#include <d3d12.h>

namespace HIKARI::MESHRENDERER {

    // MeshRenderer が PostSystem に直接触れないための、フレーム単位の入力テクスチャ契約。
    struct MeshPassResources {
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv{};
        int fallbackAoTextureHandle = -1;
    };

} // namespace HIKARI::MESHRENDERER
