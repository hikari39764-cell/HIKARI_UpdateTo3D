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
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Core/HIKARI_Logger.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI::GFX {

namespace {
	// バックバッファに対するレターボックスの位置とサイズを計算する。座標とサイズはバックバッファに対するピクセル単位。
    struct LetterboxRect {
        float x;
        float y;
        float width;
        float height;
    };
	// 画面サイズとバックバッファサイズから、レターボックスの位置とサイズを計算する。
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
	// エラーコードをログに出力する
void LogHr(const char* stage, HRESULT hr) {
    char buf[256]{};
    std::snprintf(buf, sizeof(buf), "[Dx12Core] %s failed. hr=0x%08lX\n", stage, static_cast<unsigned long>(hr));
    OutputDebugStringA(buf);
    DEBUGLOG::PushRenderError(buf);
    HIKARI_LOG_ERROR(buf);
}

}
// Dx12Core の初期化。失敗した場合は false を返す。
bool Dx12Core::Initialize(HWND hwnd, int w, int h, bool enableDebugLayer) {
    HIKARI_LOG_D3D12("Dx12Core initialization started.");

    hwnd_ = hwnd;
    width_ = w;
    height_ = h;

    GfxDebugConfig debugConfig = GetGfxDebugConfig();
    debugConfig.enableDebugLayer = enableDebugLayer && debugConfig.enableDebugLayer;
    SetGfxDebugConfig(debugConfig);
	// デバッグレイヤーと GPU ベースのバリデーションを有効にする
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
    dsvDesc.NumDescriptors = 2;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_));
    if (FAILED(hr)) {
        LogHr("Create DSV Heap", hr);
        return false;
    }
    SetD3D12Name(dsvHeap_.Get(), L"HIKARI Main DSV Heap");
    dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    HIKARI_LOG_D3D12("DSV heap created.");

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = DESCRIPTOR::kSrvHeapCapacity;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));
    if (FAILED(hr)) {
        LogHr("Create SRV Heap", hr);
        return false;
    }
    SetD3D12Name(srvHeap_.Get(), L"HIKARI Global SRV Heap");
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    HIKARI_LOG_D3D12("SRV heap created. descriptors=4096.");

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
// スワップチェインのバックバッファを取得し、RTV を作成する
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
        resourceStates_.Track(backBuffers_[i].Get(), D3D12_RESOURCE_STATE_PRESENT);
        rtv.ptr += rtvDescriptorSize_;
    }
}
// 深度バッファを作成し、DSV と SRV を作成する
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
    resourceStates_.Track(depthBuffer_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &dsv, dsvHeap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    readOnlyDsvHandle.ptr += dsvDescriptorSize_;

    D3D12_DEPTH_STENCIL_VIEW_DESC readOnlyDsv{};
    readOnlyDsv.Format = DXGI_FORMAT_D32_FLOAT;
    readOnlyDsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    readOnlyDsv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &readOnlyDsv, readOnlyDsvHandle);

    const UINT sceneDepthSrvIndex =
        DESCRIPTOR::ToIndex(DESCRIPTOR::SystemSrv::SceneDepth);

    sceneDepthSrvCpu_ =
        DESCRIPTOR::CpuAt(srvHeap_.Get(), srvDescriptorSize_, sceneDepthSrvIndex);
    sceneDepthSrvGpu_ =
        DESCRIPTOR::GpuAt(srvHeap_.Get(), srvDescriptorSize_, sceneDepthSrvIndex);

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
// Dx12Core の終了処理。GPU の完了を待ち、リソースを解放する。
void Dx12Core::Shutdown() {
    WaitGPU();
    deferredReleaseQueue_.FlushAll();
    if (fenceEvent_) CloseHandle(fenceEvent_);
    fenceEvent_ = nullptr;
}
// フレームの開始処理
void Dx12Core::BeginFrame(float clearR, float clearG, float clearB, float clearA) {
    allocators_[frameIndex_]->Reset();
    cmdList_->Reset(allocators_[frameIndex_].Get(), nullptr);
    PIX::BeginGpuEvent(cmdList_.Get(), PIX::kColorFrame, "Frame");

    resourceStates_.Transition(
        cmdList_.Get(),
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);

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
// フレームの終了処理
void Dx12Core::EndFrame() {
    resourceStates_.Transition(
        cmdList_.Get(),
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT);
    PIX::EndGpuEvent(cmdList_.Get());

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
// GPU の完了を待ち、GPU が使用しているリソースの解放を行う
void Dx12Core::WaitGPU() {
    const uint64_t signal = fenceValue_;
    queue_->Signal(fence_.Get(), signal);
    fenceValue_++;
    if (fence_->GetCompletedValue() < signal) {
        fence_->SetEventOnCompletion(signal, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    deferredReleaseQueue_.Collect(fence_->GetCompletedValue());
}
// フレームを進める。現在のフレームの完了を待ち、次のフレームのバックバッファを取得する。
void Dx12Core::MoveToNextFrame() {
    const uint64_t signal = fenceValue_;
    queue_->Signal(fence_.Get(), signal);
    fenceValue_++;

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    if (fence_->GetCompletedValue() < signal) {
        fence_->SetEventOnCompletion(signal, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    deferredReleaseQueue_.Collect(fence_->GetCompletedValue());
}
// ウィンドウサイズの変更に伴うリソースの再作成。GPU の完了を待ち、古いリソースを解放してから、新しいスワップチェインのバッファと深度バッファを作成する。
void Dx12Core::Resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    WaitGPU();

    width_ = w;
    height_ = h;

    resourceStates_.Reset();

    for (auto& bb : backBuffers_) bb.Reset();
    depthBuffer_.Reset();

    swapChain_->ResizeBuffers(kFrameCount, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    CreateSwapChainResources();
    CreateDepthBuffer();
}
// 現在のフレームの RTV ハンドルを取得する
D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::CurrentRTV() const {
    auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += rtvDescriptorSize_ * frameIndex_;
    return handle;
}
// DSV ハンドルを取得する
D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::DSV() const {
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}
// 読み取り専用 DSV ハンドルを取得する
D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::ReadOnlyDSV() const {
    auto handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += dsvDescriptorSize_;
    return handle;
}
// 深度バッファの SRV ハンドルを取得する
D3D12_GPU_DESCRIPTOR_HANDLE Dx12Core::SceneDepthSrv() const {
    return sceneDepthSrvGpu_;
}
// 深度バッファのリソースを取得する
ID3D12Resource* Dx12Core::SceneDepthResource() const {
    return depthBuffer_.Get();
}
// 現在のフレームのバックバッファリソースを取得する
ID3D12Resource* Dx12Core::CurrentBackBuffer() {
    return backBuffers_[frameIndex_].Get();
}
// GPU による遅延解放の保留数を取得する
size_t Dx12Core::GetPendingDeferredReleaseCount() const {
    return deferredReleaseQueue_.GetPendingCount();
}
// GPU fence の完了状態をポーリングする。
bool Dx12Core::IsFenceComplete(uint64_t fenceValue) const {
    return fence_ != nullptr && fence_->GetCompletedValue() >= fenceValue;
}
// 現在のコンテキストを構築して返す。Context には、コマンドリストやリソースのハンドルなど、描画に必要な情報が含まれる。
Context Dx12Core::BuildContext() const {
    Context ctx{};
    ctx.device = device_.Get();
    ctx.cmdList = cmdList_.Get();
    ctx.queue = queue_.Get();
    ctx.srvHeap = srvHeap_.Get();
    ctx.rtv = CurrentRTV();
    ctx.dsv = DSV();
    ctx.readOnlyDsv = ReadOnlyDSV();
    ctx.sceneDepthSrvCpu = sceneDepthSrvCpu_;
    ctx.sceneDepthSrv = SceneDepthSrv();
    ctx.sceneDepthResource = SceneDepthResource();
    ctx.resourceStates = const_cast<ResourceStateTracker*>(&resourceStates_);
    ctx.deferredReleaseQueue = const_cast<GpuDeferredReleaseQueue*>(&deferredReleaseQueue_);
    ctx.currentFrameRetireFenceValue = fenceValue_;
    ctx.frameIndex = frameIndex_;
    ctx.backBufferWidth = width_;
    ctx.backBufferHeight = height_;
    return ctx;
}

} // namespace HIKARI::GFX
