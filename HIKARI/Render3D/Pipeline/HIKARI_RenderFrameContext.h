#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HIKARI {
    class Camera3D;
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {

    struct RenderFrameContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        Camera3D* camera = nullptr;
        const SceneEnvironment* environment = nullptr;
        uint32_t width = 1;
        uint32_t height = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv{};
        bool hasSceneDepthSrv = false;
        bool hasSceneColorSrv = false;
    };

    struct ScreenSpacePassContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        uint32_t width = 1;
        uint32_t height = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        bool depthReadable = false;
    };

} // namespace HIKARI::RENDER3D::PIPELINE
