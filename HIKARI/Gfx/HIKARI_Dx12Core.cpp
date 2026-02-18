#include "HIKARI_Dx12Core.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <cassert>

using Microsoft::WRL::ComPtr;

namespace HIKARI::GFX {

bool Dx12Core::Initialize(HWND hwnd, int w, int h, bool enableDebugLayer) {
    hwnd_ = hwnd;
    width_ = w;
    height_ = h;

#ifdef _DEBUG
    if (enableDebugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
        }
    }
#endif

    UINT factoryFlags = 0;
#ifdef _DEBUG
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) return false;

    for (UINT i = 0; factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 desc{};
        adapter_->GetDesc3(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
    }

    hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device_->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue_));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = static_cast<UINT>(w);
    scDesc.Height = static_cast<UINT>(h);
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = kFrameCount;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1;
    hr = factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1);
    if (FAILED(hr)) return false;
    hr = sc1.As(&swapChain_);
    if (FAILED(hr)) return false;

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kFrameCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_));
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_));

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = 2048;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));

    CreateSwapChainResources();
    CreateDepthBuffer();

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i]));
    }
    device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[frameIndex_].Get(), nullptr, IID_PPV_ARGS(&cmdList_));
    cmdList_->Close();

    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    fenceValue_ = 1;
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    return true;
}

void Dx12Core::CreateSwapChainResources() {
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, rtv);
        rtv.ptr += rtvDescriptorSize_;
    }
}

void Dx12Core::CreateDepthBuffer() {
    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, static_cast<UINT64>(width_), static_cast<UINT>(height_), 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depthBuffer_));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &dsv, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

void Dx12Core::Shutdown() {
    WaitGPU();
    if (fenceEvent_) CloseHandle(fenceEvent_);
    fenceEvent_ = nullptr;
}

void Dx12Core::BeginFrame(float clearR, float clearG, float clearB, float clearA) {
    allocators_[frameIndex_]->Reset();
    cmdList_->Reset(allocators_[frameIndex_].Get(), nullptr);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList_->ResourceBarrier(1, &barrier);

    auto rtv = CurrentRTV();
    auto dsv = DSV();
    cmdList_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const float color[] = { clearR, clearG, clearB, clearA };
    cmdList_->ClearRenderTargetView(rtv, color, 0, nullptr);
    cmdList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp{ 0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1 };
    D3D12_RECT sc{ 0,0,width_,height_ };
    cmdList_->RSSetViewports(1, &vp);
    cmdList_->RSSetScissorRects(1, &sc);
}

void Dx12Core::EndFrame() {
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmdList_->ResourceBarrier(1, &barrier);

    cmdList_->Close();
    ID3D12CommandList* lists[] = { cmdList_.Get() };
    queue_->ExecuteCommandLists(1, lists);
    swapChain_->Present(1, 0);

    MoveToNextFrame();
}

void Dx12Core::WaitGPU() {
    const uint64_t signal = fenceValue_;
    queue_->Signal(fence_.Get(), signal);
    fenceValue_++;
    if (fence_->GetCompletedValue() < signal) {
        fence_->SetEventOnCompletion(signal, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void Dx12Core::MoveToNextFrame() {
    const uint64_t signal = fenceValue_;
    queue_->Signal(fence_.Get(), signal);
    fenceValue_++;

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    if (fence_->GetCompletedValue() < signal) {
        fence_->SetEventOnCompletion(signal, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void Dx12Core::Resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    WaitGPU();

    width_ = w;
    height_ = h;

    for (auto& bb : backBuffers_) bb.Reset();
    depthBuffer_.Reset();

    swapChain_->ResizeBuffers(kFrameCount, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    CreateSwapChainResources();
    CreateDepthBuffer();
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::CurrentRTV() const {
    auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += rtvDescriptorSize_ * frameIndex_;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::DSV() const {
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

ID3D12Resource* Dx12Core::CurrentBackBuffer() {
    return backBuffers_[frameIndex_].Get();
}

Context Dx12Core::BuildContext() const {
    Context ctx{};
    ctx.device = device_.Get();
    ctx.cmdList = cmdList_.Get();
    ctx.queue = queue_.Get();
    ctx.srvHeap = srvHeap_.Get();
    ctx.rtv = CurrentRTV();
    ctx.dsv = DSV();
    ctx.frameIndex = frameIndex_;
    ctx.backBufferWidth = width_;
    ctx.backBufferHeight = height_;
    return ctx;
}

} // namespace HIKARI::GFX
