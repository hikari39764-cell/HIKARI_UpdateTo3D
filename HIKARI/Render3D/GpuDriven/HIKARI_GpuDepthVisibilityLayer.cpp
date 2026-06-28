#include "Render3D/GpuDriven/HIKARI_GpuDepthVisibilityLayer.h"

#include <algorithm>
#include <array>
#include <string>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorAllocator.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        constexpr DXGI_FORMAT kHzbFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr uint32_t kHzbThreadGroupSize = 8u;
        constexpr uint32_t kHzbMaxMipCount = 16u;

        uint32_t ComputeMipCount(uint32_t width, uint32_t height) {
            uint32_t mipCount = 0;
            width = std::max(1u, (std::max(1u, width) + 1u) / 2u);
            height = std::max(1u, (std::max(1u, height) + 1u) / 2u);
            while (mipCount < kHzbMaxMipCount) {
                ++mipCount;
                if (width == 1u && height == 1u) {
                    break;
                }
                width = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
            }
            return std::max(1u, mipCount);
        }

        uint32_t DivRoundUp(uint32_t value, uint32_t divisor) {
            return (value + divisor - 1u) / divisor;
        }

        uint64_t CurrentRetireFenceValue() {
            return SERVICES::gCtx.currentFrameRetireFenceValue != 0
                ? SERVICES::gCtx.currentFrameRetireFenceValue
                : 0;
        }

        template <typename T>
        void RetireD3D12Object(Microsoft::WRL::ComPtr<T>& object, const char* debugName) {
            if (object == nullptr) {
                return;
            }

            Microsoft::WRL::ComPtr<T> retired = object;
            object.Reset();

            GFX::GpuDeferredReleaseQueue* queue = SERVICES::gCtx.deferredReleaseQueue;
            const uint64_t retireFence = CurrentRetireFenceValue();
            if (queue != nullptr && retireFence != 0) {
                queue->Enqueue(
                    retireFence,
                    [retired]() mutable {
                        retired.Reset();
                    },
                    debugName != nullptr ? debugName : "GpuDepthVisibility.Resource");
                return;
            }

            retired.Reset();
        }

        struct DepthVisibilityDescriptorState {
            GFX::Context context{};
            GFX::DescriptorAllocator allocator{};
            UINT descriptorSize = 0;
            bool initialized = false;
        };

        DepthVisibilityDescriptorState& DepthVisibilityDescriptors() {
            static DepthVisibilityDescriptorState state{};
            return state;
        }

        bool EnsureDepthVisibilityDescriptorAllocator() {
            DepthVisibilityDescriptorState& state = DepthVisibilityDescriptors();
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr) {
                return false;
            }

            if (!state.initialized ||
                state.context.device != device ||
                state.context.srvHeap != heap) {
                state.context = SERVICES::gCtx;
                state.descriptorSize = device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                state.allocator.Initialize(
                    GFX::DESCRIPTOR::kGpuDepthVisibilityTransientDescriptorBegin,
                    GFX::DESCRIPTOR::kGpuDepthVisibilityTransientDescriptorCount);
                state.initialized = true;
            }
            return true;
        }

        RenderResourceView AllocateDepthVisibilityDescriptor() {
            RenderResourceView view{};
            if (!EnsureDepthVisibilityDescriptorAllocator()) {
                return view;
            }

            DepthVisibilityDescriptorState& state = DepthVisibilityDescriptors();
            const GFX::DescriptorSlot slot = state.allocator.Allocate();
            if (!slot.IsValid()) {
                HIKARI_LOG_ERROR("[GpuDepthVisibility][ERROR] transient descriptor pool exhausted.");
                return view;
            }

            view.descriptorIndex = slot.index;
            view.cpu = GFX::DESCRIPTOR::CpuAt(
                state.context.srvHeap,
                state.descriptorSize,
                slot.index);
            view.gpu = GFX::DESCRIPTOR::GpuAt(
                state.context.srvHeap,
                state.descriptorSize,
                slot.index);
            return view;
        }

        void FreeDepthVisibilityDescriptor(RenderResourceView view) {
            if (view.descriptorIndex == UINT32_MAX) {
                return;
            }

            DepthVisibilityDescriptorState& state = DepthVisibilityDescriptors();
            const GFX::DescriptorSlot slot{ view.descriptorIndex };
            if (!state.initialized ||
                !state.allocator.Owns(slot) ||
                !state.allocator.IsAllocated(slot)) {
                return;
            }

            state.allocator.Free(slot);
        }

        void RetireDescriptor(RenderResourceView view, const char* debugName) {
            if (!view.IsValid()) {
                return;
            }

            GFX::GpuDeferredReleaseQueue* queue = SERVICES::gCtx.deferredReleaseQueue;
            const uint64_t retireFence = CurrentRetireFenceValue();
            if (queue != nullptr && retireFence != 0) {
                queue->Enqueue(
                    retireFence,
                    [view]() {
                        FreeDepthVisibilityDescriptor(view);
                    },
                    debugName != nullptr ? debugName : "GpuDepthVisibility.Descriptor");
                return;
            }

            FreeDepthVisibilityDescriptor(view);
        }

        RenderResourceView CreateTransientTexture2DSrvDescriptor(
            ID3D12Resource* resource,
            DXGI_FORMAT format,
            UINT mostDetailedMip = 0,
            UINT mipLevels = 1) {

            if (resource == nullptr || mipLevels == 0) {
                return {};
            }

            RenderResourceView view = AllocateDepthVisibilityDescriptor();
            if (!view.IsValid() || view.cpu.ptr == 0 || view.gpu.ptr == 0) {
                return {};
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = mostDetailedMip;
            srvDesc.Texture2D.MipLevels = mipLevels;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            SERVICES::gCtx.device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
            return view;
        }

        RenderResourceView CreateTransientTexture2DUavDescriptor(
            ID3D12Resource* resource,
            DXGI_FORMAT format,
            UINT mipSlice = 0) {

            if (resource == nullptr) {
                return {};
            }

            RenderResourceView view = AllocateDepthVisibilityDescriptor();
            if (!view.IsValid() || view.cpu.ptr == 0 || view.gpu.ptr == 0) {
                return {};
            }

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = mipSlice;
            uavDesc.Texture2D.PlaneSlice = 0;
            SERVICES::gCtx.device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, view.cpu);
            return view;
        }
    }

    void GpuDepthVisibilityLayer::ResetFrame() {
        const bool psoReady = hzbPso_ != nullptr && hzbRootSig_ != nullptr;
        const bool resourcesReady = !hzbMips_.empty();
        const uint32_t width = width_ != 0 ? width_ : depthWidth_;
        const uint32_t height = height_ != 0 ? height_ : depthHeight_;
        const uint32_t mipCount = static_cast<uint32_t>(hzbMips_.size());
        const uint32_t hzbWidth = !hzbMips_.empty() ? hzbMips_.front().width : 0u;
        const uint32_t hzbHeight = !hzbMips_.empty() ? hzbMips_.front().height : 0u;
        const D3D12_GPU_DESCRIPTOR_HANDLE finest =
            hzbSrv_.gpu;
        const D3D12_GPU_DESCRIPTOR_HANDLE coarsest =
            !hzbMips_.empty() ? hzbMips_.back().srv.gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
        const D3D12_GPU_DESCRIPTOR_HANDLE visibilityDepthSrv = visibilityDepthSrv_.gpu;

        stats_ = {};
        stats_.psoReady = psoReady;
        stats_.resourcesReady = resourcesReady && visibilityDepth_ != nullptr;
        stats_.width = width;
        stats_.height = height;
        stats_.hzbMipCount = mipCount;
        stats_.hzbDescriptorCount = hzbSrv_.IsValid() ? (mipCount * 2u + 1u) : 0u;
        stats_.visibilityDepthReady =
            visibilityDepth_ != nullptr && visibilityDepthSrv.ptr != 0;
        stats_.visibilityDepthSrv = visibilityDepthSrv;
        stats_.hzbWidth = hzbWidth;
        stats_.hzbHeight = hzbHeight;
        stats_.hzbFinestSrv = finest;
        stats_.hzbCoarsestSrv = coarsest;
    }

    void GpuDepthVisibilityLayer::Release() {
        ReleaseResources();
        ReleaseDepthResource();
        hzbRootSig_.Reset();
        hzbPso_.Reset();
        width_ = 0;
        height_ = 0;
        depthWidth_ = 0;
        depthHeight_ = 0;
        stats_ = {};
    }

    void GpuDepthVisibilityLayer::RecordDepthPrepass(bool written) {
        stats_.depthPrepassWritten = written;
    }

    void GpuDepthVisibilityLayer::RecordHzbViewProj(const MATH::Mat4& viewProj) {
        stats_.hzbViewProj = viewProj;
        stats_.hzbViewProjValid = true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GpuDepthVisibilityLayer::BeginDepthPrepass(
        ID3D12GraphicsCommandList* cmd,
        uint32_t width,
        uint32_t height) {

        if (cmd == nullptr || !EnsureDepthResource(width, height)) {
            return {};
        }

        TransitionDepth(cmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd->ClearDepthStencilView(
            visibilityDsv_,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr);

        stats_.visibilityDepthReady = visibilityDepthSrv_.gpu.ptr != 0;
        stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
        return visibilityDsv_;
    }

    bool GpuDepthVisibilityLayer::BuildHzb(
        ID3D12GraphicsCommandList* cmd,
        uint32_t width,
        uint32_t height) {

        stats_.visibilityDepthReady =
            visibilityDepth_ != nullptr &&
            visibilityDepthSrv_.gpu.ptr != 0 &&
            width != 0 &&
            height != 0;
        stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
        if (cmd == nullptr || !stats_.visibilityDepthReady) {
            return false;
        }

        TransitionDepth(
            cmd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        return BuildHzbFromSource(
            cmd,
            width,
            height,
            visibilityDepthSrv_.gpu,
            "GpuDepthVisibility.BuildHZB.DepthPrepass");
    }

    bool GpuDepthVisibilityLayer::BuildHzbFromDepthSrv(
        ID3D12GraphicsCommandList* cmd,
        uint32_t width,
        uint32_t height,
        D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv,
        const MATH::Mat4& hzbViewProj) {

        stats_.visibilityDepthReady =
            sourceDepthSrv.ptr != 0 &&
            width != 0 &&
            height != 0;
        stats_.visibilityDepthSrv = sourceDepthSrv;
        if (!BuildHzbFromSource(
            cmd,
            width,
            height,
            sourceDepthSrv,
            "GpuDepthVisibility.BuildHZB.SceneDepth")) {
            return false;
        }

        RecordHzbViewProj(hzbViewProj);
        return true;
    }

    bool GpuDepthVisibilityLayer::BuildHzbFromSource(
        ID3D12GraphicsCommandList* cmd,
        uint32_t width,
        uint32_t height,
        D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv,
        const char* pixEventName) {

        stats_.hzbBuildRequested = true;
        stats_.hzbBuilt = false;
        if (cmd == nullptr ||
            sourceDepthSrv.ptr == 0 ||
            width == 0 ||
            height == 0) {
            return false;
        }

        if (!EnsurePipeline() || !EnsureResources(width, height)) {
            return false;
        }

        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        if (srvHeap == nullptr ||
            hzbTexture_ == nullptr ||
            hzbSrv_.gpu.ptr == 0 ||
            hzbMips_.empty()) {
            return false;
        }
        stats_.descriptorPoolReady = true;

        GFX::PIX::ScopedGpuEvent pix(
            cmd,
            GFX::PIX::kColorUpload,
            pixEventName != nullptr ? pixEventName : "GpuDepthVisibility.BuildHZB");

        ID3D12DescriptorHeap* heaps[] = { srvHeap };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(hzbRootSig_.Get());
        cmd->SetPipelineState(hzbPso_.Get());

        D3D12_GPU_DESCRIPTOR_HANDLE sourceSrv = sourceDepthSrv;
        uint32_t sourceWidth = std::max(1u, width);
        uint32_t sourceHeight = std::max(1u, height);

        for (uint32_t mipIndex = 0; mipIndex < static_cast<uint32_t>(hzbMips_.size()); ++mipIndex) {
            HzbMipView& mip = hzbMips_[mipIndex];
            TransitionHzbMip(cmd, mipIndex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            const uint32_t constants[] = {
                sourceWidth,
                sourceHeight,
                mip.width,
                mip.height
            };
            cmd->SetComputeRoot32BitConstants(0, 4u, constants, 0u);
            cmd->SetComputeRootDescriptorTable(1, sourceSrv);
            cmd->SetComputeRootDescriptorTable(2, mip.uav.gpu);
            cmd->Dispatch(
                DivRoundUp(mip.width, kHzbThreadGroupSize),
                DivRoundUp(mip.height, kHzbThreadGroupSize),
                1u);

            const D3D12_RESOURCE_BARRIER uavBarrier =
                CD3DX12_RESOURCE_BARRIER::UAV(hzbTexture_.Get());
            cmd->ResourceBarrier(1, &uavBarrier);
            TransitionHzbMip(
                cmd,
                mipIndex,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            sourceSrv = mip.srv.gpu;
            sourceWidth = mip.width;
            sourceHeight = mip.height;
        }

        stats_.hzbBuilt = true;
        stats_.resourcesReady = true;
        stats_.visibilityDepthReady = sourceDepthSrv.ptr != 0;
        stats_.visibilityDepthSrv = sourceDepthSrv;
        stats_.hzbFinestSrv = hzbSrv_.gpu;
        stats_.hzbCoarsestSrv = hzbMips_.back().srv.gpu;
        return true;
    }

    bool GpuDepthVisibilityLayer::EnsurePipeline() {
        if (hzbRootSig_ != nullptr && hzbPso_ != nullptr) {
            stats_.psoReady = true;
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }
        if (!GFX::SupportsShaderModel6(device)) {
            DEBUGLOG::PushRenderError(
                "[GpuDepthVisibility][ERROR] Shader Model 6.0 is required for HZB build.");
            return false;
        }

        std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        std::array<D3D12_ROOT_PARAMETER, 3> params{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 4;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &ranges[0];
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &ranges[1];

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = static_cast<UINT>(params.size());
        rootDesc.pParameters = params.data();
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            signature.GetAddressOf(),
            error.GetAddressOf());
        if (FAILED(hr)) {
            if (error != nullptr) {
                OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            }
            return false;
        }
        hr = device->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(hzbRootSig_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "GpuDepthVisibility::CreateRootSignature")) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> cs;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/GpuDepthVisibility_BuildHzbCS.hlsl",
            "main",
            GFX::ShaderStage::Compute,
            cs.GetAddressOf())) {
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = hzbRootSig_.Get();
        psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        hr = device->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(hzbPso_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "GpuDepthVisibility::CreateHzbPso")) {
            return false;
        }
        hzbPso_->SetName(L"HIKARI.GpuDepthVisibility.HZB.PSO");
        stats_.psoReady = true;
        return true;
    }

    bool GpuDepthVisibilityLayer::EnsureDepthResource(uint32_t width, uint32_t height) {
        width = std::max(1u, width);
        height = std::max(1u, height);
        if (depthWidth_ == width &&
            depthHeight_ == height &&
            visibilityDepth_ != nullptr &&
            visibilityDepthSrv_.IsValid() &&
            visibilityDsv_.ptr != 0) {
            stats_.visibilityDepthReady = true;
            stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }

        ReleaseDepthResource();
        depthWidth_ = width;
        depthHeight_ = height;

        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS,
            static_cast<UINT64>(width),
            height,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(visibilityDepth_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "GpuDepthVisibility::CreateVisibilityDepth")) {
            ReleaseDepthResource();
            return false;
        }
        visibilityDepth_->SetName(L"HIKARI.DepthVisibility.Depth");
        visibilityDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = device->CreateDescriptorHeap(
            &dsvHeapDesc,
            IID_PPV_ARGS(visibilityDsvHeap_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "GpuDepthVisibility::CreateVisibilityDsvHeap")) {
            ReleaseDepthResource();
            return false;
        }
        visibilityDsvHeap_->SetName(L"HIKARI.DepthVisibility.DSVHeap");
        visibilityDsv_ = visibilityDsvHeap_->GetCPUDescriptorHandleForHeapStart();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        device->CreateDepthStencilView(visibilityDepth_.Get(), &dsvDesc, visibilityDsv_);

        visibilityDepthSrv_ = CreateTransientTexture2DSrvDescriptor(
            visibilityDepth_.Get(),
            DXGI_FORMAT_R32_FLOAT);
        if (!visibilityDepthSrv_.IsValid()) {
            ReleaseDepthResource();
            return false;
        }

        stats_.visibilityDepthReady = true;
        stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
        stats_.width = depthWidth_;
        stats_.height = depthHeight_;
        HIKARI_LOG_INFO(
            "[GpuDepthVisibility] visibility depth resized " +
            std::to_string(depthWidth_) +
            "x" +
            std::to_string(depthHeight_));
        return true;
    }

    bool GpuDepthVisibilityLayer::EnsureResources(uint32_t width, uint32_t height) {
        width = std::max(1u, width);
        height = std::max(1u, height);
        if (width_ == width &&
            height_ == height &&
            hzbTexture_ != nullptr &&
            hzbSrv_.IsValid() &&
            !hzbMips_.empty()) {
            stats_.resourcesReady = true;
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }

        ReleaseResources();
        width_ = width;
        height_ = height;

        const uint32_t mipCount = ComputeMipCount(width, height);
        hzbMips_.reserve(mipCount);

        const uint32_t mip0Width = std::max(1u, (width + 1u) / 2u);
        const uint32_t mip0Height = std::max(1u, (height + 1u) / 2u);
        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            kHzbFormat,
            mip0Width,
            mip0Height,
            1,
            static_cast<UINT16>(mipCount),
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const D3D12_RESOURCE_STATES initialState =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            nullptr,
            IID_PPV_ARGS(hzbTexture_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "GpuDepthVisibility::CreateHzbTexture")) {
            ReleaseResources();
            return false;
        }
        hzbTexture_->SetName(L"HIKARI.DepthVisibility.HZB");

        hzbSrv_ = CreateTransientTexture2DSrvDescriptor(
            hzbTexture_.Get(),
            kHzbFormat,
            0,
            mipCount);
        if (!hzbSrv_.IsValid() || hzbSrv_.gpu.ptr == 0) {
            ReleaseResources();
            return false;
        }

        uint32_t mipWidth = mip0Width;
        uint32_t mipHeight = mip0Height;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex) {
            HzbMipView mip{};
            mip.width = mipWidth;
            mip.height = mipHeight;

            mip.srv = CreateTransientTexture2DSrvDescriptor(
                hzbTexture_.Get(),
                kHzbFormat,
                mipIndex,
                1);
            mip.uav = CreateTransientTexture2DUavDescriptor(
                hzbTexture_.Get(),
                kHzbFormat,
                mipIndex);
            if (!mip.srv.IsValid() || !mip.uav.IsValid()) {
                RetireDescriptor(mip.srv, "GpuDepthVisibility.HZB.MipSRV.FailedCreate");
                RetireDescriptor(mip.uav, "GpuDepthVisibility.HZB.MipUAV.FailedCreate");
                ReleaseResources();
                return false;
            }

            hzbMips_.push_back(std::move(mip));
            if (mipWidth == 1u && mipHeight == 1u) {
                break;
            }
            mipWidth = std::max(1u, mipWidth / 2u);
            mipHeight = std::max(1u, mipHeight / 2u);
        }

        stats_.resourcesReady = hzbTexture_ != nullptr && hzbSrv_.IsValid() && !hzbMips_.empty();
        stats_.width = width_;
        stats_.height = height_;
        stats_.hzbWidth = !hzbMips_.empty() ? hzbMips_.front().width : 0u;
        stats_.hzbHeight = !hzbMips_.empty() ? hzbMips_.front().height : 0u;
        stats_.hzbMipCount = static_cast<uint32_t>(hzbMips_.size());
        stats_.hzbDescriptorCount = stats_.hzbMipCount * 2u + 1u;
        if (!hzbMips_.empty()) {
            stats_.hzbFinestSrv = hzbSrv_.gpu;
            stats_.hzbCoarsestSrv = hzbMips_.back().srv.gpu;
        }
        HIKARI_LOG_INFO(
            "[GpuDepthVisibility] HZB resized " +
            std::to_string(stats_.hzbWidth) +
            "x" +
            std::to_string(stats_.hzbHeight) +
            " source=" +
            std::to_string(width_) +
            "x" +
            std::to_string(height_) +
            " mips=" +
            std::to_string(hzbMips_.size()));
        return stats_.resourcesReady;
    }

    void GpuDepthVisibilityLayer::ReleaseResources() {
        RetireDescriptor(hzbSrv_, "GpuDepthVisibility.HZB.SRV");
        hzbSrv_ = {};
        for (HzbMipView& mip : hzbMips_) {
            RetireDescriptor(mip.srv, "GpuDepthVisibility.HZB.MipSRV");
            RetireDescriptor(mip.uav, "GpuDepthVisibility.HZB.MipUAV");
            mip = {};
        }
        hzbMips_.clear();
        RetireD3D12Object(hzbTexture_, "GpuDepthVisibility.HZB.Texture");
        width_ = 0;
        height_ = 0;
    }

    void GpuDepthVisibilityLayer::ReleaseDepthResource() {
        RetireDescriptor(visibilityDepthSrv_, "GpuDepthVisibility.VisibilityDepth.SRV");
        visibilityDepthSrv_ = {};
        visibilityDsv_ = {};
        RetireD3D12Object(visibilityDsvHeap_, "GpuDepthVisibility.VisibilityDepth.DSVHeap");
        RetireD3D12Object(visibilityDepth_, "GpuDepthVisibility.VisibilityDepth.Texture");
        visibilityDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthWidth_ = 0;
        depthHeight_ = 0;
    }

    void GpuDepthVisibilityLayer::TransitionDepth(
        ID3D12GraphicsCommandList* cmd,
        D3D12_RESOURCE_STATES nextState) {

        if (cmd == nullptr ||
            visibilityDepth_ == nullptr ||
            visibilityDepthState_ == nextState) {
            visibilityDepthState_ = nextState;
            return;
        }

        const D3D12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                visibilityDepth_.Get(),
                visibilityDepthState_,
                nextState);
        cmd->ResourceBarrier(1, &barrier);
        visibilityDepthState_ = nextState;
    }

    void GpuDepthVisibilityLayer::TransitionHzbMip(
        ID3D12GraphicsCommandList* cmd,
        uint32_t mipIndex,
        D3D12_RESOURCE_STATES nextState) {

        if (mipIndex >= hzbMips_.size()) {
            return;
        }

        HzbMipView& mip = hzbMips_[mipIndex];
        if (cmd == nullptr || hzbTexture_ == nullptr || mip.state == nextState) {
            mip.state = nextState;
            return;
        }

        const D3D12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                hzbTexture_.Get(),
                mip.state,
                nextState,
                mipIndex);
        cmd->ResourceBarrier(1, &barrier);
        mip.state = nextState;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
