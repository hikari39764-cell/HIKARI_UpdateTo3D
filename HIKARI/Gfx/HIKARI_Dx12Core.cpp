#include "HIKARI_Dx12Core.h"
#include "../HIKARI_Utility.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <cassert>
#include <cstdio>
#include <string>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Core/HIKARI_Logger.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI::GFX {

namespace {
    constexpr UINT kSceneDepthSrvIndex = 2046;

    struct LetterboxRect {
        float x;
        float y;
        float width;
        float height;
    };

    static LetterboxRect ComputeLetterboxRect(int backBufferW, int backBufferH) {
        if (backBufferW <= 0 || backBufferH <= 0) {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }

        const float targetAspect = static_cast<float>(kScreenW) / static_cast<float>(kScreenH);
        const float backBufferAspect = static_cast<float>(backBufferW) / static_cast<float>(backBufferH);

        int vpW = backBufferW;
        int vpH = backBufferH;
        int vpX = 0;
        int vpY = 0;

        if (backBufferAspect > targetAspect) {
            vpW = static_cast<int>(static_cast<float>(backBufferH) * targetAspect + 0.5f);
            vpX = (backBufferW - vpW) / 2;
        } else {
            vpH = static_cast<int>(static_cast<float>(backBufferW) / targetAspect + 0.5f);
            vpY = (backBufferH - vpH) / 2;
        }

        return {
            static_cast<float>(vpX),
            static_cast<float>(vpY),
            static_cast<float>(vpW),
            static_cast<float>(vpH)
        };
    }
}

namespace {

void LogHr(const char* stage, HRESULT hr) {
    char buf[256]{};
    std::snprintf(buf, sizeof(buf), "[Dx12Core] %s failed. hr=0x%08lX\n", stage, static_cast<unsigned long>(hr));
    OutputDebugStringA(buf);
    DEBUGLOG::PushRenderError(buf);
    HIKARI_LOG_ERROR(buf);
}

}

bool Dx12Core::Initialize(HWND hwnd, int w, int h, bool enableDebugLayer) {
    HIKARI_LOG_D3D12("Dx12Core initialization started.");

    hwnd_ = hwnd;
    width_ = w;
    height_ = h;

    GfxDebugConfig debugConfig = GetGfxDebugConfig();
    debugConfig.enableDebugLayer = enableDebugLayer && debugConfig.enableDebugLayer;
    SetGfxDebugConfig(debugConfig);

#ifdef _DEBUG
    if (debugConfig.enableDebugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            DEBUGLOG::WriteRenderLogLine("[D3D12] Debug Layer enabled.");
            HIKARI_LOG_D3D12("Debug Layer enabled.");

            if (debugConfig.enableGpuBasedValidation) {
                ComPtr<ID3D12Debug3> debug3;
                if (SUCCEEDED(debug.As(&debug3))) {
                    debug3->SetEnableGPUBasedValidation(TRUE);
                    DEBUGLOG::WriteRenderLogLine("[D3D12] GPU-Based Validation enabled.");
                    HIKARI_LOG_D3D12("GPU-Based Validation enabled.");
                }
            }
        }
    }
#endif

    UINT factoryFlags = 0;
#ifdef _DEBUG
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        // Graphics Tools / DXGI Debug が無い環境では失敗するため、通常 Factory にフォールバックする。
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
        if (FAILED(hr)) {
            LogHr("CreateDXGIFactory2", hr);
            return false;
        }
    }
    HIKARI_LOG_D3D12("DXGI factory created.");

    for (UINT i = 0; factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 desc{};
        adapter_->GetDesc3(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
    }
    HIKARI_LOG_D3D12("Adapter selected.");

    hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) {
        // adapter_ が取れない環境向けフォールバック
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
        if (FAILED(hr)) {
            LogHr("D3D12CreateDevice", hr);
            return false;
        }
    }
    ConfigureD3D12InfoQueue(device_.Get());
    SetD3D12Name(device_.Get(), L"HIKARI D3D12 Device");
    HIKARI_LOG_D3D12("Device created.");

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device_->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue_));
    if (FAILED(hr)) {
        LogHr("CreateCommandQueue", hr);
        return false;
    }
    SetD3D12Name(queue_.Get(), L"HIKARI Direct Command Queue");
    HIKARI_LOG_D3D12("Command queue created.");

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
    if (FAILED(hr)) {
        LogHr("CreateSwapChainForHwnd", hr);
        return false;
    }
    hr = sc1.As(&swapChain_);
    if (FAILED(hr)) {
        LogHr("SwapChain Cast to IDXGISwapChain4", hr);
        return false;
    }
    HIKARI_LOG_D3D12("SwapChain created. buffers=3.");

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kFrameCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_));
    if (FAILED(hr)) {
        LogHr("Create RTV Heap", hr);
        return false;
    }
    SetD3D12Name(rtvHeap_.Get(), L"HIKARI SwapChain RTV Heap");
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    HIKARI_LOG_D3D12("RTV heap created.");

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_));
    if (FAILED(hr)) {
        LogHr("Create DSV Heap", hr);
        return false;
    }
    SetD3D12Name(dsvHeap_.Get(), L"HIKARI Main DSV Heap");
    HIKARI_LOG_D3D12("DSV heap created.");

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = 2048;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));
    if (FAILED(hr)) {
        LogHr("Create SRV Heap", hr);
        return false;
    }
    SetD3D12Name(srvHeap_.Get(), L"HIKARI Global SRV Heap");
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    HIKARI_LOG_D3D12("SRV heap created. descriptors=2048.");

    CreateSwapChainResources();
    HIKARI_LOG_D3D12("SwapChain resources created.");
    CreateDepthBuffer();
    HIKARI_LOG_D3D12("Depth buffer created.");

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i]));
        if (FAILED(hr)) {
            LogHr("CreateCommandAllocator", hr);
            return false;
        }
    }
    HIKARI_LOG_D3D12("Command allocators created.");
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[frameIndex_].Get(), nullptr, IID_PPV_ARGS(&cmdList_));
    if (FAILED(hr)) {
        LogHr("CreateCommandList", hr);
        return false;
    }
    SetD3D12Name(cmdList_.Get(), L"HIKARI Main Command List");
    cmdList_->Close();
    HIKARI_LOG_D3D12("Command list created.");

    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) {
        LogHr("CreateFence", hr);
        return false;
    }
    SetD3D12Name(fence_.Get(), L"HIKARI Frame Fence");
    HIKARI_LOG_D3D12("Fence created.");
    fenceValue_ = 1;
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        HIKARI_LOG_ERROR("[Dx12Core] CreateEvent failed.");
        return false;
    }
    HIKARI_LOG_D3D12("Dx12Core initialization completed.");
    return true;
}

void Dx12Core::CreateSwapChainResources() {
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        const HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
        if (!HIKARI_DX_CHECK(hr, "Dx12Core::CreateSwapChainResources GetBuffer")) {
            continue;
        }
        const std::wstring name = L"HIKARI SwapChain BackBuffer[" + std::to_wstring(i) + L"]";
        SetD3D12Name(backBuffers_[i].Get(), name.c_str());
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, rtv);
        rtv.ptr += rtvDescriptorSize_;
    }
}

void Dx12Core::CreateDepthBuffer() {
    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS, static_cast<UINT64>(width_), static_cast<UINT>(height_), 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depthBuffer_));
    if (!HIKARI_DX_CHECK(hr, "Dx12Core::CreateDepthBuffer")) {
        return;
    }
    SetD3D12Name(depthBuffer_.Get(), L"HIKARI Main Depth Buffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &dsv, dsvHeap_->GetCPUDescriptorHandleForHeapStart());

    const auto cpuStart = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    const auto gpuStart = srvHeap_->GetGPUDescriptorHandleForHeapStart();

    sceneDepthSrvCpu_.ptr =
        cpuStart.ptr + static_cast<SIZE_T>(srvDescriptorSize_) * kSceneDepthSrvIndex;
    sceneDepthSrvGpu_.ptr =
        gpuStart.ptr + static_cast<UINT64>(srvDescriptorSize_) * kSceneDepthSrvIndex;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;

    device_->CreateShaderResourceView(depthBuffer_.Get(), &srv, sceneDepthSrvCpu_);
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

    const auto letterbox = ComputeLetterboxRect(width_, height_);

    const float black[] = { 0.0f, 0.0f, 0.0f, clearA };
    cmdList_->ClearRenderTargetView(rtv, black, 0, nullptr);

    const float color[] = { clearR, clearG, clearB, clearA };
    const bool hasBlackBars =
        (letterbox.x > 0.0f) || (letterbox.y > 0.0f) ||
        (letterbox.width < static_cast<float>(width_)) ||
        (letterbox.height < static_cast<float>(height_));

    if (!hasBlackBars) {
        cmdList_->ClearRenderTargetView(rtv, color, 0, nullptr);
    }

    cmdList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp{ letterbox.x, letterbox.y, letterbox.width, letterbox.height, 0, 1 };
    D3D12_RECT sc{
        static_cast<LONG>(letterbox.x),
        static_cast<LONG>(letterbox.y),
        static_cast<LONG>(letterbox.x + letterbox.width),
        static_cast<LONG>(letterbox.y + letterbox.height)
    };
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

    if (GetGfxDebugConfig().dumpInfoQueueOnFrameEnd) {
        DumpD3D12InfoQueue(device_.Get(), "EndFrame");
        ClearD3D12InfoQueue(device_.Get());
    }

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

D3D12_GPU_DESCRIPTOR_HANDLE Dx12Core::SceneDepthSrv() const {
    return sceneDepthSrvGpu_;
}

ID3D12Resource* Dx12Core::SceneDepthResource() const {
    return depthBuffer_.Get();
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
    ctx.sceneDepthSrv = SceneDepthSrv();
    ctx.sceneDepthResource = SceneDepthResource();
    ctx.frameIndex = frameIndex_;
    ctx.backBufferWidth = width_;
    ctx.backBufferHeight = height_;
    return ctx;
}

} // namespace HIKARI::GFX
