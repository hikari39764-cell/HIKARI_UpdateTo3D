#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <d3dx12.h>
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {

    class RenderTarget2D
    {
    public:
        RenderTarget2D() = default;
        ~RenderTarget2D() { Finalize(); }

        RenderTarget2D(const RenderTarget2D&) = delete;
        RenderTarget2D& operator=(const RenderTarget2D&) = delete;

        bool Init(int width, int height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, bool withDepth = false);
        void UpdateContext(const HIKARI::GFX::Context& ctx);
        void Finalize();

        void BeginCapture(float r = 0, float g = 0, float b = 0, float a = 0, float depthClear = 1.0f);
        void Rebind();
        void EndCapture();
        int GetWidth() const { return width_; }
        int GetHeight() const { return height_; }
        DXGI_FORMAT GetFormat() const { return format_; }
        bool HasDepth() const { return hasDepth_; }

        ID3D12Resource* GetResource() const { return colorTex_.Get(); }
        ID3D12Resource* GetDepthResource() const { return depthTex_.Get(); }

        ID3D12DescriptorHeap* GetSrvHeap() const { return srvHeap_.Get(); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpu() const { return srvGpuHandle_; }

    private:
        bool CreateResources();

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> colorTex_;
        D3D12_RESOURCE_STATES colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D12Resource> depthTex_;
        D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};

        D3D12_VIEWPORT viewport_{};
        D3D12_RECT scissorRect_{};

        int width_ = 0;
        int height_ = 0;
        DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
        bool initialized_ = false;
        bool hasDepth_ = false;
        HIKARI::GFX::Context context_{};
    };

} // namespace HIKARI
