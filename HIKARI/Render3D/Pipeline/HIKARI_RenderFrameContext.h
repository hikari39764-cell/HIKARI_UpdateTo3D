#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HIKARI {
    class Camera3D;
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {

    // Pipeline が所有する render target 操作を、下位 pass へ明示的に渡すための境界。
    struct RenderTargetAccess {
        using RebindCallback = bool (*)(void* userData);
        using BeginDepthReadCallback = bool (*)(void* userData);
        using EndDepthReadCallback = void (*)(void* userData);

        void* userData = nullptr;
        RebindCallback rebind = nullptr;
        BeginDepthReadCallback beginDepthRead = nullptr;
        EndDepthReadCallback endDepthRead = nullptr;

        bool Rebind() const {
            return rebind != nullptr && rebind(userData);
        }

        bool BeginDepthRead() const {
            return beginDepthRead != nullptr && beginDepthRead(userData);
        }

        void EndDepthRead() const {
            if (endDepthRead != nullptr) {
                endDepthRead(userData);
            }
        }
    };

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
        RenderTargetAccess renderTargetAccess{};
    };

} // namespace HIKARI::RENDER3D::PIPELINE
