#pragma once

#include "HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ResourceStateTracker.h"

#include <array>
#include <cstddef>
#include <wrl.h>
#include <dxgi1_6.h>

namespace HIKARI::GFX {

class Dx12Core {
public:
    static constexpr uint32_t kFrameCount = 3;

    bool Initialize(HWND hwnd, int w, int h, bool enableDebugLayer);
    void Shutdown();

    void BeginFrame(float clearR, float clearG, float clearB, float clearA);
    void EndFrame();
    void Resize(int w, int h);

    uint32_t FrameIndex() const { return frameIndex_; }
    ID3D12Device* Device() { return device_.Get(); }
    ID3D12GraphicsCommandList* CmdList() { return cmdList_.Get(); }
    ID3D12CommandQueue* Queue() { return queue_.Get(); }
    ID3D12DescriptorHeap* SrvHeap() { return srvHeap_.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DSV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyDSV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE SceneDepthSrv() const;
    ID3D12Resource* SceneDepthResource() const;
    ID3D12Resource* CurrentBackBuffer();
    size_t GetPendingDeferredReleaseCount() const;

    Context BuildContext() const;

private:
    void WaitGPU();
    void MoveToNextFrame();
    void CreateSwapChainResources();
    void CreateDepthBuffer();

private:
    HWND hwnd_{};
    int width_{};
    int height_{};

    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> allocators_{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> backBuffers_{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT rtvDescriptorSize_{};
    UINT dsvDescriptorSize_{};
    UINT srvDescriptorSize_{};
    D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSrvCpu_{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrvGpu_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
    ResourceStateTracker resourceStates_;
    GpuDeferredReleaseQueue deferredReleaseQueue_;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    uint64_t fenceValue_{};
    HANDLE fenceEvent_{};
    uint32_t frameIndex_{};
};

} // namespace HIKARI::GFX
