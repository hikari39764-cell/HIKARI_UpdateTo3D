#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDepthVisibilityStats {
        bool psoReady = false;
        bool resourcesReady = false;
        bool depthPrepassWritten = false;
        bool hzbBuilt = false;
        bool hzbBuildRequested = false;
        bool descriptorPoolReady = false;
        bool visibilityDepthReady = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t hzbWidth = 0;
        uint32_t hzbHeight = 0;
        uint32_t hzbMipCount = 0;
        uint32_t hzbDescriptorCount = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE visibilityDepthSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE hzbFinestSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE hzbCoarsestSrv{};
        MATH::Mat4 hzbViewProj{};
        bool hzbViewProjValid = false;
    };

    class GpuDepthVisibilityLayer final {
    public:
        void ResetFrame();
        void Release();

        void RecordDepthPrepass(bool written);
        void RecordHzbViewProj(const MATH::Mat4& viewProj);
        D3D12_CPU_DESCRIPTOR_HANDLE BeginDepthPrepass(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height);
        bool BuildHzb(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height);
        bool BuildHzbFromDepthSrv(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height,
            D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv,
            const MATH::Mat4& hzbViewProj);

        const GpuDepthVisibilityStats& GetStats() const { return stats_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetVisibilityDepthSrv() const { return visibilityDepthSrv_.gpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetHzbFinestSrv() const { return stats_.hzbFinestSrv; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetHzbCoarsestSrv() const { return stats_.hzbCoarsestSrv; }

    private:
        struct HzbMipView {
            RenderResourceView srv{};
            RenderResourceView uav{};
            D3D12_RESOURCE_STATES state =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        bool EnsurePipeline();
        bool EnsureResources(uint32_t width, uint32_t height);
        bool EnsureDepthResource(uint32_t width, uint32_t height);
        bool BuildHzbFromSource(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height,
            D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv,
            const char* pixEventName);
        void ReleaseResources();
        void ReleaseDepthResource();
        void TransitionDepth(
            ID3D12GraphicsCommandList* cmd,
            D3D12_RESOURCE_STATES nextState);
        void TransitionHzbMip(
            ID3D12GraphicsCommandList* cmd,
            uint32_t mipIndex,
            D3D12_RESOURCE_STATES nextState);

        Microsoft::WRL::ComPtr<ID3D12RootSignature> hzbRootSig_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> hzbPso_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> visibilityDepth_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> visibilityDsvHeap_{};
        RenderResourceView visibilityDepthSrv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE visibilityDsv_{};
        D3D12_RESOURCE_STATES visibilityDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        Microsoft::WRL::ComPtr<ID3D12Resource> hzbTexture_{};
        RenderResourceView hzbSrv_{};
        std::vector<HzbMipView> hzbMips_{};
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        uint32_t depthWidth_ = 0;
        uint32_t depthHeight_ = 0;
        GpuDepthVisibilityStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
