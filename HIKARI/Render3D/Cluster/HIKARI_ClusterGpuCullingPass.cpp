#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"

#include <algorithm>
#include <iterator>

#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        constexpr uint32_t kThreadGroupSize = 64u;
        constexpr uint32_t kClusterCullMergeGapIndexLimit = 384u;
        constexpr uint32_t kClusterCullMergeRunGapIndexBudget = 2048u;
        constexpr uint32_t kClusterCullMergeMaxIndexSpan = 8192u;
        // クラスタ間の空白をまたぐ結合は過剰描画になりやすいので、正式なcompactまで無効化する。
        constexpr uint32_t kClusterCullMergeClusterGapLimit = 0u;

        constexpr UINT AlignConstantBufferSize(size_t size) {
            return static_cast<UINT>((size + 255u) & ~255u);
        }

        size_t NextCapacity(size_t requested, size_t fallback) {
            size_t capacity = (std::max)(requested, fallback);
            size_t pow2 = 1u;
            while (pow2 < capacity) {
                pow2 <<= 1u;
            }
            return pow2;
        }

        void ReportWarning(const char* message) {
            if (message == nullptr) {
                return;
            }
            DEBUGLOG::PushRenderError(message);
            HIKARI_LOG_WARN(message);
        }

        ClusterDrawCullModeBucket ResolveCullBucket(uint32_t flags) {
            constexpr uint32_t doubleSidedFlag =
                static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneInstanceFlags::DoubleSided);
            return (flags & doubleSidedFlag) != 0u
                ? ClusterDrawCullModeBucket::DoubleSided
                : ClusterDrawCullModeBucket::BackFace;
        }

        size_t CullBucketIndex(ClusterDrawCullModeBucket bucket) {
            const size_t index = static_cast<size_t>(bucket);
            return index < kClusterDrawCullModeBucketCount ? index : 0u;
        }

        bool CreateComputePipelineState(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            const char* entryPoint,
            const wchar_t* debugName,
            ID3D12PipelineState** outPipelineState) {

            if (device == nullptr ||
                rootSignature == nullptr ||
                entryPoint == nullptr ||
                outPipelineState == nullptr) {
                return false;
            }

            Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
            if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_ClusterCullCS.hlsl",
                entryPoint,
                GFX::ShaderStage::Compute,
                computeShader.GetAddressOf())) {
                return false;
            }

            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = rootSignature;
            psoDesc.CS = {
                computeShader->GetBufferPointer(),
                computeShader->GetBufferSize()
            };
            const HRESULT hr = device->CreateComputePipelineState(
                &psoDesc,
                IID_PPV_ARGS(outPipelineState));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateComputePipelineState")) {
                return false;
            }
            GFX::SetD3D12Name(*outPipelineState, debugName);
            return true;
        }

    }

    bool ClusterGpuCullingPass::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* drawRootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount,
        size_t pageTaskCapacity,
        size_t visibleRangeCapacity,
        size_t drawArgumentCapacity) {

        Reset();
        if (device == nullptr) {
            return false;
        }
        if (!EnsurePipeline(device)) {
            ReportWarning("[ClusterGpuCulling][WARN] pipeline creation failed.");
            return false;
        }
        if (!EnsureDispatchCommandSignature(device)) {
            ReportWarning("[ClusterGpuCulling][WARN] dispatch command signature creation failed.");
            return false;
        }
        if (!EnsureDrawCommandSignature(
            device,
            drawRootSignature,
            rootConstantParameterIndex,
            rootConstantCount)) {
            ReportWarning("[ClusterGpuCulling][WARN] draw command signature creation failed.");
            return false;
        }
        if (!EnsureMeshletDispatchCommandSignature(
            device,
            drawRootSignature,
            rootConstantParameterIndex,
            rootConstantCount)) {
            ReportWarning("[ClusterGpuCulling][WARN] meshlet dispatch command signature creation failed.");
        }
        if (!EnsureCapacity(device, pageTaskCapacity, visibleRangeCapacity, drawArgumentCapacity)) {
            ReportWarning("[ClusterGpuCulling][WARN] resource allocation failed.");
            return false;
        }
        stats_.initialized = true;
        stats_.psoReady =
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr;
        stats_.threadGroupSize = kThreadGroupSize;
        return true;
    }

    void ClusterGpuCullingPass::Reset() {
        constantsMapped_ = nullptr;
        counterResetMapped_ = nullptr;
        constantsUploadBuffer_.Reset();
        counterResetUploadBuffer_.Reset();
        pageTaskBuffer_.Reset();
        visibleRangeBuffer_.Reset();
        drawArgumentBuffer_.Reset();
        meshletDispatchArgumentBuffer_.Reset();
        dispatchArgumentBuffer_.Reset();
        counterBuffer_.Reset();
        for (CounterReadbackSlot& slot : counterReadbackSlots_) {
            slot.buffer.Reset();
            slot.resolved = false;
        }
        rootSignature_.Reset();
        expandPageTasksPipelineState_.Reset();
        finalizeDispatchPipelineState_.Reset();
        cullPageTasksPipelineState_.Reset();
        dispatchCommandSignature_.Reset();
        drawCommandSignature_.Reset();
        meshletDispatchCommandSignature_.Reset();
        pageTaskCapacity_ = 0;
        visibleRangeCapacity_ = 0;
        drawArgumentCapacity_ = 0;
        counterReadbackWriteIndex_ = 0;
        latestGpuCounters_ = {};
        latestGpuCountersValid_ = false;
        pageTaskBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        visibleRangeBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        drawArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        counterBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        stats_ = {};
    }

    bool ClusterGpuCullingPass::EnsurePipeline(ID3D12Device* device) {
        if (rootSignature_ != nullptr &&
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }
        if (!GFX::SupportsShaderModel6(device)) {
            ReportWarning("[ClusterGpuCulling][WARN] Shader Model 6.0 is not supported.");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE clusterGeometryPoolRange{};
        clusterGeometryPoolRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        clusterGeometryPoolRange.NumDescriptors = GFX::DESCRIPTOR::kSystemSrvDynamicCount;
        clusterGeometryPoolRange.BaseShaderRegister = 0;
        clusterGeometryPoolRange.RegisterSpace = 1;
        clusterGeometryPoolRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[9]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Descriptor.ShaderRegister = 0;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].Descriptor.ShaderRegister = 17;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[2].Descriptor.ShaderRegister = 0;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[3].Descriptor.ShaderRegister = 1;

        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[4].Descriptor.ShaderRegister = 2;

        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[5].DescriptorTable.NumDescriptorRanges = 1;
        params[5].DescriptorTable.pDescriptorRanges = &clusterGeometryPoolRange;

        params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[6].Descriptor.ShaderRegister = 3;

        params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[7].Descriptor.ShaderRegister = 4;

        params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[8].Descriptor.ShaderRegister = 5;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = static_cast<UINT>(std::size(params));
        rootDesc.pParameters = params;
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            signature.GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(hr)) {
            if (errors != nullptr) {
                OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            }
            return false;
        }
        hr = device->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(rootSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateRootSignature")) {
            return false;
        }
        GFX::SetD3D12Name(rootSignature_.Get(), L"Cluster GPU Culling Root Signature");

        if (!CreateComputePipelineState(
            device,
            rootSignature_.Get(),
            "ExpandPageTasksCS",
            L"Cluster GPU Expand Page Tasks PSO",
            expandPageTasksPipelineState_.GetAddressOf())) {
            return false;
        }
        if (!CreateComputePipelineState(
            device,
            rootSignature_.Get(),
            "FinalizePageTaskDispatchCS",
            L"Cluster GPU Finalize Page Task Dispatch PSO",
            finalizeDispatchPipelineState_.GetAddressOf())) {
            return false;
        }
        if (!CreateComputePipelineState(
            device,
            rootSignature_.Get(),
            "CullPageTasksCS",
            L"Cluster GPU Cull Page Tasks PSO",
            cullPageTasksPipelineState_.GetAddressOf())) {
            return false;
        }
        return true;
    }

    bool ClusterGpuCullingPass::EnsureDispatchCommandSignature(ID3D12Device* device) {
        if (dispatchCommandSignature_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDesc{};
        argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride = static_cast<UINT>(sizeof(D3D12_DISPATCH_ARGUMENTS));
        signatureDesc.NumArgumentDescs = 1;
        signatureDesc.pArgumentDescs = &argumentDesc;

        const HRESULT hr = device->CreateCommandSignature(
            &signatureDesc,
            nullptr,
            IID_PPV_ARGS(dispatchCommandSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateDispatchCommandSignature")) {
            dispatchCommandSignature_.Reset();
            return false;
        }
        GFX::SetD3D12Name(dispatchCommandSignature_.Get(), L"Cluster GPU Cull Dispatch Signature");
        return true;
    }

    bool ClusterGpuCullingPass::EnsureDrawCommandSignature(
        ID3D12Device* device,
        ID3D12RootSignature* drawRootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount) {

        if (drawCommandSignature_ != nullptr) {
            return true;
        }
        if (device == nullptr ||
            drawRootSignature == nullptr ||
            rootConstantCount != 4u) {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2]{};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[0].Constant.RootParameterIndex = rootConstantParameterIndex;
        argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
        argumentDescs[0].Constant.Num32BitValuesToSet = rootConstantCount;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride = static_cast<UINT>(sizeof(GpuIndirectDrawArgument));
        signatureDesc.NumArgumentDescs = static_cast<UINT>(std::size(argumentDescs));
        signatureDesc.pArgumentDescs = argumentDescs;

        const HRESULT hr = device->CreateCommandSignature(
            &signatureDesc,
            drawRootSignature,
            IID_PPV_ARGS(drawCommandSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateDrawCommandSignature")) {
            drawCommandSignature_.Reset();
            return false;
        }
        GFX::SetD3D12Name(drawCommandSignature_.Get(), L"Cluster GPU Draw Command Signature");
        return true;
    }

    bool ClusterGpuCullingPass::EnsureMeshletDispatchCommandSignature(
        ID3D12Device* device,
        ID3D12RootSignature* drawRootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount) {

        if (meshletDispatchCommandSignature_ != nullptr) {
            return true;
        }
        if (device == nullptr ||
            drawRootSignature == nullptr ||
            rootConstantCount != 4u) {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2]{};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[0].Constant.RootParameterIndex = rootConstantParameterIndex;
        argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
        argumentDescs[0].Constant.Num32BitValuesToSet = rootConstantCount;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride = static_cast<UINT>(sizeof(GpuIndirectMeshletDispatchArgument));
        signatureDesc.NumArgumentDescs = static_cast<UINT>(std::size(argumentDescs));
        signatureDesc.pArgumentDescs = argumentDescs;

        const HRESULT hr = device->CreateCommandSignature(
            &signatureDesc,
            drawRootSignature,
            IID_PPV_ARGS(meshletDispatchCommandSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateMeshletDispatchCommandSignature")) {
            meshletDispatchCommandSignature_.Reset();
            return false;
        }
        GFX::SetD3D12Name(
            meshletDispatchCommandSignature_.Get(),
            L"Cluster GPU Meshlet Dispatch Command Signature");
        return true;
    }

    bool ClusterGpuCullingPass::EnsureCapacity(
        ID3D12Device* device,
        size_t pageTaskCapacity,
        size_t visibleRangeCapacity,
        size_t drawArgumentCapacity) {

        if (device == nullptr) {
            return false;
        }

        const size_t requestedPageTaskCapacity =
            NextCapacity(pageTaskCapacity, kDefaultClusterGpuPageTaskCapacity);
        const size_t requestedVisibleCapacity =
            NextCapacity(visibleRangeCapacity, kDefaultClusterGpuCullingVisibleRangeCapacity);
        const size_t requestedDrawArgumentCapacity =
            NextCapacity(drawArgumentCapacity, kDefaultClusterGpuDrawArgumentCapacity);
        if (constantsUploadBuffer_ != nullptr &&
            counterResetUploadBuffer_ != nullptr &&
            pageTaskBuffer_ != nullptr &&
            visibleRangeBuffer_ != nullptr &&
            drawArgumentBuffer_ != nullptr &&
            meshletDispatchArgumentBuffer_ != nullptr &&
            dispatchArgumentBuffer_ != nullptr &&
            counterBuffer_ != nullptr &&
            counterReadbackSlots_[0].buffer != nullptr &&
            pageTaskCapacity_ >= requestedPageTaskCapacity &&
            visibleRangeCapacity_ >= requestedVisibleCapacity &&
            drawArgumentCapacity_ >= requestedDrawArgumentCapacity) {
            return true;
        }

        constantsMapped_ = nullptr;
        counterResetMapped_ = nullptr;
        constantsUploadBuffer_.Reset();
        counterResetUploadBuffer_.Reset();
        pageTaskBuffer_.Reset();
        visibleRangeBuffer_.Reset();
        drawArgumentBuffer_.Reset();
        meshletDispatchArgumentBuffer_.Reset();
        dispatchArgumentBuffer_.Reset();
        counterBuffer_.Reset();
        for (CounterReadbackSlot& slot : counterReadbackSlots_) {
            slot.buffer.Reset();
            slot.resolved = false;
        }
        pageTaskCapacity_ = 0;
        visibleRangeCapacity_ = 0;
        drawArgumentCapacity_ = 0;
        counterReadbackWriteIndex_ = 0;
        latestGpuCounters_ = {};
        latestGpuCountersValid_ = false;
        pageTaskBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        visibleRangeBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        drawArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        counterBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;

        const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto readbackHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

        auto constantsDesc =
            CD3DX12_RESOURCE_DESC::Buffer(AlignConstantBufferSize(sizeof(GpuConstants)));
        HRESULT hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &constantsDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(constantsUploadBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateConstantsUpload")) {
            return false;
        }
        if (FAILED(constantsUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantsMapped_)))) {
            constantsMapped_ = nullptr;
            return false;
        }
        GFX::SetD3D12Name(constantsUploadBuffer_.Get(), L"Cluster GPU Culling Constants");

        auto counterUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(GpuCounters));
        hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterUploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(counterResetUploadBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterResetUpload")) {
            return false;
        }
        if (FAILED(counterResetUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&counterResetMapped_)))) {
            counterResetMapped_ = nullptr;
            return false;
        }
        GFX::SetD3D12Name(counterResetUploadBuffer_.Get(), L"Cluster GPU Culling Counter Reset");

        const UINT64 pageTaskBytes =
            static_cast<UINT64>(sizeof(GpuPageTask)) *
            static_cast<UINT64>(requestedPageTaskCapacity);
        auto pageTaskDesc = CD3DX12_RESOURCE_DESC::Buffer(
            pageTaskBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &pageTaskDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(pageTaskBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreatePageTaskBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(pageTaskBuffer_.Get(), L"Cluster GPU Page Tasks");

        const UINT64 visibleBytes =
            static_cast<UINT64>(sizeof(GpuVisibleRange)) *
            static_cast<UINT64>(requestedVisibleCapacity);
        auto visibleDesc = CD3DX12_RESOURCE_DESC::Buffer(
            visibleBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &visibleDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(visibleRangeBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateVisibleRangeBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(visibleRangeBuffer_.Get(), L"Cluster GPU Culling Visible Ranges");

        const UINT64 drawArgumentBytes =
            static_cast<UINT64>(sizeof(GpuIndirectDrawArgument)) *
            static_cast<UINT64>(requestedDrawArgumentCapacity);
        auto drawArgumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            drawArgumentBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &drawArgumentDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(drawArgumentBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateDrawArgumentBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(drawArgumentBuffer_.Get(), L"Cluster GPU Draw Arguments");

        const UINT64 meshletDispatchArgumentBytes =
            static_cast<UINT64>(sizeof(GpuIndirectMeshletDispatchArgument)) *
            static_cast<UINT64>(requestedDrawArgumentCapacity);
        auto meshletDispatchArgumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            meshletDispatchArgumentBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &meshletDispatchArgumentDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(meshletDispatchArgumentBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateMeshletDispatchArgumentBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(
            meshletDispatchArgumentBuffer_.Get(),
            L"Cluster GPU Meshlet Dispatch Arguments");

        auto dispatchArgumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            sizeof(D3D12_DISPATCH_ARGUMENTS),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &dispatchArgumentDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(dispatchArgumentBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateDispatchArguments")) {
            return false;
        }
        GFX::SetD3D12Name(dispatchArgumentBuffer_.Get(), L"Cluster GPU Cull Dispatch Arguments");

        auto counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
            sizeof(GpuCounters),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(counterBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(counterBuffer_.Get(), L"Cluster GPU Culling Counters");

        auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(GpuCounters));
        for (size_t i = 0; i < counterReadbackSlots_.size(); ++i) {
            hr = device->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(counterReadbackSlots_[i].buffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterReadback")) {
                return false;
            }
            GFX::SetD3D12Name(
                counterReadbackSlots_[i].buffer.Get(),
                L"Cluster GPU Culling Counter Readback");
        }

        pageTaskCapacity_ = requestedPageTaskCapacity;
        visibleRangeCapacity_ = requestedVisibleCapacity;
        drawArgumentCapacity_ = requestedDrawArgumentCapacity;
        return true;
    }

    void ClusterGpuCullingPass::ResetFrameStats() {
        const bool initialized = rootSignature_ != nullptr &&
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr &&
            constantsUploadBuffer_ != nullptr &&
            counterResetUploadBuffer_ != nullptr &&
            pageTaskBuffer_ != nullptr &&
            visibleRangeBuffer_ != nullptr &&
            drawArgumentBuffer_ != nullptr &&
            meshletDispatchArgumentBuffer_ != nullptr &&
            dispatchArgumentBuffer_ != nullptr &&
            counterBuffer_ != nullptr &&
            dispatchCommandSignature_ != nullptr;
        stats_ = {};
        stats_.initialized = initialized;
        stats_.psoReady =
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr;
        stats_.inputBufferReady = true;
        stats_.pageTaskBufferReady = pageTaskBuffer_ != nullptr;
        stats_.visibleRangeBufferReady = visibleRangeBuffer_ != nullptr;
        stats_.drawArgumentBufferReady = drawArgumentBuffer_ != nullptr;
        stats_.dispatchArgumentBufferReady = dispatchArgumentBuffer_ != nullptr;
        stats_.drawCommandSignatureReady = drawCommandSignature_ != nullptr;
        stats_.counterBufferReady = counterBuffer_ != nullptr;
        stats_.inputCapacity = 0;
        stats_.pageTaskCapacity = pageTaskCapacity_;
        stats_.visibleRangeCapacity = visibleRangeCapacity_;
        stats_.drawArgumentCapacity = drawArgumentCapacity_;
        stats_.threadGroupSize = kThreadGroupSize;
        stats_.gpuCounterReadbackReady = counterReadbackSlots_[0].buffer != nullptr;
        stats_.gpuCounterReadbackValid = latestGpuCountersValid_;
        if (latestGpuCountersValid_) {
            stats_.gpuInputCount = latestGpuCounters_.inputCount;
            stats_.gpuPageTaskCount = latestGpuCounters_.pageTaskCount;
            stats_.gpuPageTaskOverflowCount = latestGpuCounters_.pageTaskOverflowCount;
            stats_.gpuVisibleRangeCount = latestGpuCounters_.visibleRangeCount;
            stats_.gpuVisibleClusterCount = latestGpuCounters_.visibleClusterCount;
            stats_.gpuOverflowCount = latestGpuCounters_.overflowCount;
            stats_.gpuInputFrustumCulledCount =
                latestGpuCounters_.inputFrustumCulledCount;
            stats_.gpuPageTestedCount =
                latestGpuCounters_.pageTestedCount;
            stats_.gpuPageFrustumCulledCount =
                latestGpuCounters_.pageFrustumCulledCount;
            stats_.gpuClusterTestedCount =
                latestGpuCounters_.clusterTestedCount;
            stats_.gpuClusterFrustumCulledCount =
                latestGpuCounters_.clusterFrustumCulledCount;
            stats_.gpuClusterConeCulledCount =
                latestGpuCounters_.clusterConeCulledCount;
            stats_.gpuClusterConeTestedCount =
                latestGpuCounters_.clusterConeTestedCount;
            stats_.gpuDoubleSidedClusterCount =
                latestGpuCounters_.doubleSidedClusterCount;
            stats_.gpuBackFaceDrawCommandCount =
                latestGpuCounters_.backFaceDrawCommandCount;
            stats_.gpuDoubleSidedDrawCommandCount =
                latestGpuCounters_.doubleSidedDrawCommandCount;
            stats_.gpuDrawCommandCount =
                stats_.gpuBackFaceDrawCommandCount +
                stats_.gpuDoubleSidedDrawCommandCount;
            stats_.gpuBackFaceDrawCommandOverflowCount =
                latestGpuCounters_.backFaceDrawCommandOverflowCount;
            stats_.gpuDoubleSidedDrawCommandOverflowCount =
                latestGpuCounters_.doubleSidedDrawCommandOverflowCount;
            stats_.gpuDrawCommandOverflowCount =
                stats_.gpuBackFaceDrawCommandOverflowCount +
                stats_.gpuDoubleSidedDrawCommandOverflowCount;
            stats_.gpuMergedGapCount =
                latestGpuCounters_.mergedGapCount;
            stats_.gpuMergedGapIndexCount =
                latestGpuCounters_.mergedGapIndexCount;
        }
    }

    void ClusterGpuCullingPass::CollectCounterReadback(CounterReadbackSlot& slot) {
        if (!slot.resolved || slot.buffer == nullptr) {
            return;
        }

        const D3D12_RANGE readRange{ 0, sizeof(GpuCounters) };
        void* mapped = nullptr;
        if (FAILED(slot.buffer->Map(0, &readRange, &mapped)) || mapped == nullptr) {
            return;
        }
        latestGpuCounters_ = *static_cast<const GpuCounters*>(mapped);
        const D3D12_RANGE writeRange{ 0, 0 };
        slot.buffer->Unmap(0, &writeRange);
        slot.resolved = false;
        latestGpuCountersValid_ = true;
    }

    void ClusterGpuCullingPass::QueueCounterReadback(ID3D12GraphicsCommandList* commandList) {
        if (commandList == nullptr || counterBuffer_ == nullptr) {
            return;
        }

        CounterReadbackSlot& slot =
            counterReadbackSlots_[counterReadbackWriteIndex_ % counterReadbackSlots_.size()];
        ++counterReadbackWriteIndex_;
        if (slot.buffer == nullptr) {
            return;
        }

        // GPU 側の可視数は次以降のフレームで読む。ここでは待機せず readback だけ積む。
        commandList->CopyBufferRegion(
            slot.buffer.Get(),
            0,
            counterBuffer_.Get(),
            0,
            sizeof(GpuCounters));
        slot.resolved = true;
    }

    void ClusterGpuCullingPass::BuildRangeStats(
        const ClusterGpuCullingSourceRange* ranges,
        size_t rangeCount) {

        if (ranges == nullptr || rangeCount == 0) {
            return;
        }

        for (size_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex) {
            const ClusterGpuCullingSourceRange& range = ranges[rangeIndex];
            stats_.sourceInstanceCount += range.instanceCount;
            const uint32_t explicitBucketCount =
                range.singleSidedInstanceCount + range.doubleSidedInstanceCount;
            if (explicitBucketCount > 0u) {
                stats_.sourceSingleSidedInstanceCount += range.singleSidedInstanceCount;
                stats_.sourceDoubleSidedInstanceCount += range.doubleSidedInstanceCount;
            }
            else {
                stats_.sourceSingleSidedInstanceCount += range.instanceCount;
            }
        }

        // GPU scene driven の主線では、CPU は候補圧縮を行わない。
        // ここでは dispatch seed 数だけを記録し、実際の candidate/draw 数は GPU counter で読む。
        stats_.candidateInstanceCount = stats_.sourceInstanceCount;
        stats_.submittedInstanceCount = stats_.sourceInstanceCount;
        stats_.sourcePageTaskCount = stats_.sourceInstanceCount;
        stats_.submittedPageTaskCount = stats_.sourceInstanceCount;
        stats_.submittedDrawSeedCount = stats_.sourceInstanceCount;
    }

    bool ClusterGpuCullingPass::Dispatch(
        ID3D12GraphicsCommandList* commandList,
        const MATH::Mat4& viewProj,
        const MATH::Vec3& cameraPosition,
        D3D12_GPU_DESCRIPTOR_HANDLE clusterGeometryPoolSrv,
        D3D12_GPU_VIRTUAL_ADDRESS surfaceGpuSceneGpuAddress,
        const ClusterGpuCullingSourceRange* ranges,
        size_t rangeCount) {

        if (!counterReadbackSlots_.empty()) {
            CollectCounterReadback(
                counterReadbackSlots_[counterReadbackWriteIndex_ % counterReadbackSlots_.size()]);
        }
        ResetFrameStats();
        BuildRangeStats(ranges, rangeCount);

        if (!stats_.initialized ||
            commandList == nullptr ||
            constantsMapped_ == nullptr ||
            counterResetMapped_ == nullptr ||
            clusterGeometryPoolSrv.ptr == 0 ||
            surfaceGpuSceneGpuAddress == 0 ||
            ranges == nullptr ||
            rangeCount == 0 ||
            stats_.sourceInstanceCount == 0) {
            return false;
        }

        const ClusterGpuCullingSourceRange& range = ranges[0];
        const uint32_t submittedCount = range.instanceCount;
        if (submittedCount == 0) {
            return false;
        }

        GpuConstants constants{};
        constants.viewProj = viewProj;
        constants.cameraPosition = {
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            0.0f
        };
        constants.inputCount = submittedCount;
        constants.visibleRangeCapacity = static_cast<uint32_t>(visibleRangeCapacity_);
        constants.drawArgumentCapacity = static_cast<uint32_t>(drawArgumentCapacity_);
        constants.enableFrustumCull = 1u;
        constants.drawArgumentBucketCapacity =
            static_cast<uint32_t>(GetDrawArgumentBucketCapacity());
        constants.clusterSrvPoolBegin = GFX::DESCRIPTOR::kSystemSrvDynamicBegin;
        constants.clusterSrvPoolCount = GFX::DESCRIPTOR::kSystemSrvDynamicCount;
        constants.enableConeCull = 1u;
        constants.enableDebugCounters =
            GFX::GetGfxDebugConfig().enableClusterGpuCullDebugCounters ? 1u : 0u;
        constants.surfaceGpuSceneBaseIndex = range.surfaceGpuSceneBaseIndex;
        constants.passKind = static_cast<uint32_t>(range.passKind);
        constants.pageTaskCapacity = static_cast<uint32_t>(pageTaskCapacity_);
        // GPU 側の draw args 圧縮は小さな index gap だけを吸収し、過剰な overdraw を上限で止める。
        constants.mergeGapIndexLimit = kClusterCullMergeGapIndexLimit;
        constants.mergeRunGapIndexBudget = kClusterCullMergeRunGapIndexBudget;
        constants.mergeMaxIndexSpan = kClusterCullMergeMaxIndexSpan;
        constants.mergeClusterGapLimit = kClusterCullMergeClusterGapLimit;
        *constantsMapped_ = constants;
        stats_.debugCountersEnabled = constants.enableDebugCounters != 0u;

        GpuCounters counters{};
        *counterResetMapped_ = counters;

        GFX::PIX::ScopedGpuEvent pix(
            commandList,
            GFX::PIX::kColorRender,
            "ClusterGpuCulling.BuildDrawArgs");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuCull(
            commandList,
            GFX::GPU_PROFILE::Pass::ClusterCull);

        if (pageTaskBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                pageTaskBuffer_.Get(),
                pageTaskBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            pageTaskBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (visibleRangeBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                visibleRangeBuffer_.Get(),
                visibleRangeBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            visibleRangeBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (drawArgumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                drawArgumentBuffer_.Get(),
                drawArgumentBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            drawArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (meshletDispatchArgumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                meshletDispatchArgumentBuffer_.Get(),
                meshletDispatchArgumentBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (dispatchArgumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                dispatchArgumentBuffer_.Get(),
                dispatchArgumentBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (counterBufferState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                counterBuffer_.Get(),
                counterBufferState_,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &barrier);
            counterBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        commandList->CopyBufferRegion(
            counterBuffer_.Get(),
            0,
            counterResetUploadBuffer_.Get(),
            0,
            sizeof(GpuCounters));

        auto counterToUav = CD3DX12_RESOURCE_BARRIER::Transition(
            counterBuffer_.Get(),
            counterBufferState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->ResourceBarrier(1, &counterToUav);
        counterBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        commandList->SetComputeRootSignature(rootSignature_.Get());
        commandList->SetComputeRootConstantBufferView(
            0,
            constantsUploadBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(
            1,
            surfaceGpuSceneGpuAddress);
        commandList->SetComputeRootUnorderedAccessView(
            2,
            visibleRangeBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            3,
            counterBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            4,
            drawArgumentBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootDescriptorTable(5, clusterGeometryPoolSrv);
        commandList->SetComputeRootUnorderedAccessView(
            6,
            pageTaskBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            7,
            dispatchArgumentBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            8,
            meshletDispatchArgumentBuffer_->GetGPUVirtualAddress());

        const UINT expandGroupCount =
            static_cast<UINT>((static_cast<size_t>(submittedCount) + kThreadGroupSize - 1u) / kThreadGroupSize);
        {
            GFX::PIX::ScopedGpuEvent expandPix(
                commandList,
                GFX::PIX::kColorRender,
                "ClusterGpuCulling.ExpandPageTasks");
            commandList->SetPipelineState(expandPageTasksPipelineState_.Get());
            commandList->Dispatch(expandGroupCount, 1u, 1u);
        }

        D3D12_RESOURCE_BARRIER expandBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(pageTaskBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(counterBuffer_.Get()),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(expandBarriers)),
            expandBarriers);

        {
            GFX::PIX::ScopedGpuEvent finalizePix(
                commandList,
                GFX::PIX::kColorRender,
                "ClusterGpuCulling.FinalizePageTaskDispatch");
            commandList->SetPipelineState(finalizeDispatchPipelineState_.Get());
            commandList->Dispatch(1u, 1u, 1u);
        }

        D3D12_RESOURCE_BARRIER finalizeBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(dispatchArgumentBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(counterBuffer_.Get()),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(finalizeBarriers)),
            finalizeBarriers);

        auto dispatchArgsToIndirect = CD3DX12_RESOURCE_BARRIER::Transition(
            dispatchArgumentBuffer_.Get(),
            dispatchArgumentBufferState_,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        commandList->ResourceBarrier(1, &dispatchArgsToIndirect);
        dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

        {
            GFX::PIX::ScopedGpuEvent cullPix(
                commandList,
                GFX::PIX::kColorRender,
                "ClusterGpuCulling.CullPageTasks");
            commandList->SetPipelineState(cullPageTasksPipelineState_.Get());
            commandList->ExecuteIndirect(
                dispatchCommandSignature_.Get(),
                1u,
                dispatchArgumentBuffer_.Get(),
                0,
                nullptr,
                0);
        }

        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(pageTaskBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(visibleRangeBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(counterBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(drawArgumentBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(meshletDispatchArgumentBuffer_.Get()),
        };
        commandList->ResourceBarrier(static_cast<UINT>(std::size(barriers)), barriers);

        auto counterToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            counterBuffer_.Get(),
            counterBufferState_,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->ResourceBarrier(1, &counterToCopy);
        counterBufferState_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
        QueueCounterReadback(commandList);

        D3D12_RESOURCE_BARRIER readyBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                visibleRangeBuffer_.Get(),
                visibleRangeBufferState_,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(
                drawArgumentBuffer_.Get(),
                drawArgumentBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
            CD3DX12_RESOURCE_BARRIER::Transition(
                meshletDispatchArgumentBuffer_.Get(),
                meshletDispatchArgumentBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
            CD3DX12_RESOURCE_BARRIER::Transition(
                counterBuffer_.Get(),
                counterBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
        };
        commandList->ResourceBarrier(static_cast<UINT>(std::size(readyBarriers)), readyBarriers);
        visibleRangeBufferState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        drawArgumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        counterBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

        stats_.dispatchCount = 3;
        stats_.workgroupCount = expandGroupCount + 1u;
        return true;
    }

    const ClusterGpuCullingPassStats& ClusterGpuCullingPass::GetStats() const {
        return stats_;
    }

    ID3D12Resource* ClusterGpuCullingPass::GetVisibleRangeBuffer() const {
        return visibleRangeBuffer_.Get();
    }

    ID3D12Resource* ClusterGpuCullingPass::GetDrawArgumentBuffer() const {
        return drawArgumentBuffer_.Get();
    }

    ID3D12Resource* ClusterGpuCullingPass::GetMeshletDispatchArgumentBuffer() const {
        return meshletDispatchArgumentBuffer_.Get();
    }

    ID3D12Resource* ClusterGpuCullingPass::GetCounterBuffer() const {
        return counterBuffer_.Get();
    }

    ID3D12CommandSignature* ClusterGpuCullingPass::GetDrawCommandSignature() const {
        return drawCommandSignature_.Get();
    }

    ID3D12CommandSignature* ClusterGpuCullingPass::GetMeshletDispatchCommandSignature() const {
        return meshletDispatchCommandSignature_.Get();
    }

    size_t ClusterGpuCullingPass::GetDrawArgumentBucketCapacity() const {
        return drawArgumentCapacity_ / kClusterDrawCullModeBucketCount;
    }

    UINT64 ClusterGpuCullingPass::GetDrawArgumentBufferOffset(
        ClusterDrawCullModeBucket bucket) const {

        return
            static_cast<UINT64>(CullBucketIndex(bucket)) *
            static_cast<UINT64>(GetDrawArgumentBucketCapacity()) *
            static_cast<UINT64>(sizeof(GpuIndirectDrawArgument));
    }

    UINT64 ClusterGpuCullingPass::GetMeshletDispatchArgumentBufferOffset(
        ClusterDrawCullModeBucket bucket) const {

        return
            static_cast<UINT64>(CullBucketIndex(bucket)) *
            static_cast<UINT64>(GetDrawArgumentBucketCapacity()) *
            static_cast<UINT64>(sizeof(GpuIndirectMeshletDispatchArgument));
    }

    UINT64 ClusterGpuCullingPass::GetDrawCommandCounterOffset(
        ClusterDrawCullModeBucket bucket) const {

        switch (bucket) {
        case ClusterDrawCullModeBucket::DoubleSided:
            return kClusterGpuCullDoubleSidedDrawCommandCounterOffsetBytes;
        case ClusterDrawCullModeBucket::BackFace:
        default:
            return kClusterGpuCullDrawCommandCounterOffsetBytes;
        }
    }

} // namespace HIKARI::RENDER3D::CLUSTER
