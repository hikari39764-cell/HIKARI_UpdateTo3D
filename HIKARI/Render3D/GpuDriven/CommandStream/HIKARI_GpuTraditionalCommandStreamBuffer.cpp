#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"

#include <algorithm>
#include <iterator>

#include <d3dx12.h>

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        constexpr uint32_t kGpuTraditionalCommandStreamThreadGroupSize = 64u;
        constexpr UINT kGpuTraditionalCommandStreamCounterStrideBytes = 32u;
        constexpr UINT kGpuTraditionalCommandStreamCounterBufferBytes =
            kGpuTraditionalCommandStreamCounterStrideBytes *
            static_cast<UINT>(kGpuDrivenPassCount) *
            static_cast<UINT>(kGpuDrivenCommandBucketCount);
        constexpr uint32_t kGpuTraditionalCommandStreamSeedFlagDoubleSided = 1u << 0;
        constexpr uint32_t kGpuTraditionalCommandStreamSeedFlagSkinned = 1u << 1;

        struct GpuTraditionalCommandPayload {
            D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
            D3D12_INDEX_BUFFER_VIEW indexBuffer{};
            D3D12_GPU_VIRTUAL_ADDRESS jointPalette = 0;
            uint32_t indexCountPerInstance = 0;
            uint32_t instanceCount = 0;
            uint32_t startIndexLocation = 0;
            int32_t baseVertexLocation = 0;
            uint32_t startInstanceLocation = 0;
            uint32_t flags = 0;
        };

        static_assert(sizeof(GpuTraditionalCommandPayload) == 64u);

        constexpr UINT AlignConstantBufferSize(size_t size) {
            return static_cast<UINT>((size + 255u) & ~255u);
        }

        template <typename T>
        void RetireD3D12Object(
            Microsoft::WRL::ComPtr<T>& object,
            const char* debugName) {

            if (object == nullptr) {
                return;
            }

            T* retired = object.Detach();
            GFX::RetireD3D12ObjectForCurrentFrame(
                retired,
                debugName != nullptr ? debugName : "GpuTraditionalCommandStream.D3D12Object");
        }

        GpuTraditionalCommandPayload ToDrawPayload(
            const RUNTIME::SurfaceDrawIndexedArgs& args) {

            GpuTraditionalCommandPayload payload{};
            payload.indexCountPerInstance = args.indexCountPerInstance;
            payload.instanceCount = args.instanceCount;
            payload.startIndexLocation = args.startIndexLocation;
            payload.baseVertexLocation = args.baseVertexLocation;
            payload.startInstanceLocation = args.startInstanceLocation;
            return payload;
        }

        bool IsIndirectDrawable(const RUNTIME::SurfaceDrawCommand& command) {
            return
                command.drawArgsValid &&
                command.drawArgs.indexCountPerInstance > 0 &&
                command.drawArgs.instanceCount > 0 &&
                command.firstGpuSceneInstanceIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
                command.HasTriangleMeshGpuView();
        }

        GpuDrivenPassKind ResolveCommandPass(
            const RUNTIME::SurfaceDrawCommand& command) {

            if (command.pass == RUNTIME::SurfaceDrawCommandPass::Shadow) {
                return GpuDrivenPassKind::Shadow;
            }
            if (command.pass == RUNTIME::SurfaceDrawCommandPass::DepthAware) {
                return GpuDrivenPassKind::ForwardDepthAware;
            }
            return command.transparent
                ? GpuDrivenPassKind::ForwardTransparent
                : GpuDrivenPassKind::ForwardOpaque;
        }

        GpuDrivenCommandBucket ResolveCommandBucket(
            const RUNTIME::SurfaceDrawCommand& command) {

            return command.doubleSided
                ? GpuDrivenCommandBucket::DoubleSided
                : GpuDrivenCommandBucket::BackFaceCulled;
        }

        uint32_t ResolveCommandBucketIndex(
            const RUNTIME::SurfaceDrawCommand& command) {

            return static_cast<uint32_t>(
                ToCommandBucketIndex(ResolveCommandBucket(command)));
        }

        struct GpuTraditionalCommandSeed {
            MATH::Vec4 boundsCenterRadius{};
            uint32_t absoluteGpuSceneInstanceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t flags = 0;
            uint32_t passIndex = 0;
            uint32_t bucketIndex = 0;
            uint32_t payloadIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t localGpuSceneInstanceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t reserved0 = 0;
            uint32_t reserved1 = 0;
        };

        static_assert(sizeof(GpuTraditionalCommandSeed) == 48u);

        struct GpuTraditionalCommandStreamCullingConstants {
            MATH::Mat4 viewProj{};
            uint32_t inputCount = 0;
            uint32_t outputCapacity = 0;
            uint32_t enableFrustumCull = 1;
            uint32_t payloadCount = 0;
        };

        bool CreateComputeRootSignature(
            ID3D12Device* device,
            ID3D12RootSignature** outRootSignature) {

            if (device == nullptr || outRootSignature == nullptr) {
                return false;
            }

            D3D12_ROOT_PARAMETER params[6]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[1].Descriptor.ShaderRegister = 0;

            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[2].Descriptor.ShaderRegister = 0;

            params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[3].Descriptor.ShaderRegister = 1;

            params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[4].Descriptor.ShaderRegister = 1;

            params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[5].Descriptor.ShaderRegister = 2;

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
                IID_PPV_ARGS(outRootSignature));
            if (!HIKARI_DX_CHECK(hr, "GpuTraditionalCommandStream::CreateComputeRootSignature")) {
                return false;
            }
            GFX::SetD3D12Name(*outRootSignature, L"GPU Traditional Command Stream GPU Culling Root Signature");
            return true;
        }

        bool CreateComputePipelineState(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            ID3D12PipelineState** outPipelineState) {

            if (device == nullptr ||
                rootSignature == nullptr ||
                outPipelineState == nullptr) {
                return false;
            }
            if (!GFX::SupportsShaderModel6(device)) {
                return false;
            }

            Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
            if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuTraditionalCommandCompactCS.hlsl",
                "CompactGpuTraditionalCommandStreamCS",
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
            if (!HIKARI_DX_CHECK(hr, "GpuTraditionalCommandStream::CreateComputePipelineState")) {
                return false;
            }
            GFX::SetD3D12Name(*outPipelineState, L"GPU Traditional Command Stream GPU Compact PSO");
            return true;
        }
    }

    bool GpuTraditionalCommandStreamBuffer::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount,
        size_t capacity) {

        if (device == nullptr ||
            rootSignature == nullptr ||
            rootConstantCount == 0 ||
            rootConstantCount != kGpuTraditionalCommandStreamRootConstantCount ||
            capacity == 0) {
            return false;
        }

        auto resetGpuBuffers = [this]() {
            RetireD3D12Object(argumentBuffer_, "GpuTraditionalCommandStream.Active.ArgumentBuffer");
            RetireD3D12Object(skinnedArgumentBuffer_, "GpuTraditionalCommandStream.Active.SkinnedArgumentBuffer");
            RetireD3D12Object(seedBuffer_, "GpuTraditionalCommandStream.Active.SeedBuffer");
            RetireD3D12Object(seedUploadBuffer_, "GpuTraditionalCommandStream.Active.SeedUploadBuffer");
            RetireD3D12Object(payloadBuffer_, "GpuTraditionalCommandStream.Active.PayloadBuffer");
            RetireD3D12Object(payloadUploadBuffer_, "GpuTraditionalCommandStream.Active.PayloadUploadBuffer");
            RetireD3D12Object(counterBuffer_, "GpuTraditionalCommandStream.Active.CounterBuffer");
            RetireD3D12Object(counterResetUploadBuffer_, "GpuTraditionalCommandStream.Active.CounterResetUploadBuffer");
            RetireD3D12Object(constantsUploadBuffer_, "GpuTraditionalCommandStream.Active.ConstantsUploadBuffer");
            seedMapped_ = nullptr;
            payloadMapped_ = nullptr;
            counterResetMapped_ = nullptr;
            constantsMapped_ = nullptr;
            argumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
            skinnedArgumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
            seedBufferState_ = D3D12_RESOURCE_STATE_COMMON;
            payloadBufferState_ = D3D12_RESOURCE_STATE_COMMON;
            counterBufferState_ = D3D12_RESOURCE_STATE_COMMON;
            for (FrameResources& frame : frameResources_) {
                RetireD3D12Object(frame.argumentBuffer, "GpuTraditionalCommandStream.Frame.ArgumentBuffer");
                RetireD3D12Object(frame.skinnedArgumentBuffer, "GpuTraditionalCommandStream.Frame.SkinnedArgumentBuffer");
                RetireD3D12Object(frame.seedBuffer, "GpuTraditionalCommandStream.Frame.SeedBuffer");
                RetireD3D12Object(frame.seedUploadBuffer, "GpuTraditionalCommandStream.Frame.SeedUploadBuffer");
                RetireD3D12Object(frame.payloadBuffer, "GpuTraditionalCommandStream.Frame.PayloadBuffer");
                RetireD3D12Object(frame.payloadUploadBuffer, "GpuTraditionalCommandStream.Frame.PayloadUploadBuffer");
                RetireD3D12Object(frame.counterBuffer, "GpuTraditionalCommandStream.Frame.CounterBuffer");
                RetireD3D12Object(frame.counterResetUploadBuffer, "GpuTraditionalCommandStream.Frame.CounterResetUploadBuffer");
                RetireD3D12Object(frame.constantsUploadBuffer, "GpuTraditionalCommandStream.Frame.ConstantsUploadBuffer");
                frame = FrameResources{};
            }
            activeFrameResourceIndex_ = 0;
        };

        resetGpuBuffers();
        RetireD3D12Object(computeRootSignature_, "GpuTraditionalCommandStream.ComputeRootSignature");
        RetireD3D12Object(compactPipelineState_, "GpuTraditionalCommandStream.CompactPipelineState");
        RetireD3D12Object(commandSignature_, "GpuTraditionalCommandStream.CommandSignature");
        RetireD3D12Object(skinnedCommandSignature_, "GpuTraditionalCommandStream.SkinnedCommandSignature");
        capacity_ = 0;
        seedCursor_ = 0;
        payloadCursor_ = 0;
        rootConstantCount_ = rootConstantCount;
        payloadIndexByGpuSceneInstance_.clear();
        stats_ = {};

        const UINT64 bufferBytes =
            static_cast<UINT64>(sizeof(GpuTraditionalCommandArgument)) *
            static_cast<UINT64>(capacity) *
            static_cast<UINT64>(kGpuDrivenPassCount) *
            static_cast<UINT64>(kGpuDrivenCommandBucketCount);
        auto argumentHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto argumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            bufferBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const UINT64 skinnedBufferBytes =
            static_cast<UINT64>(sizeof(GpuTraditionalSkinnedCommandArgument)) *
            static_cast<UINT64>(capacity) *
            static_cast<UINT64>(kGpuDrivenPassCount) *
            static_cast<UINT64>(kGpuDrivenCommandBucketCount);
        auto skinnedArgumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            skinnedBufferBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const UINT64 seedBytes =
            static_cast<UINT64>(sizeof(GpuTraditionalCommandSeed)) *
            static_cast<UINT64>(capacity);
        auto seedDesc = CD3DX12_RESOURCE_DESC::Buffer(seedBytes);
        auto seedUploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const UINT64 payloadBytes =
            static_cast<UINT64>(sizeof(GpuTraditionalCommandPayload)) *
            static_cast<UINT64>(capacity);
        auto payloadDesc = CD3DX12_RESOURCE_DESC::Buffer(payloadBytes);
        auto counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
            kGpuTraditionalCommandStreamCounterBufferBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto counterUploadDesc =
            CD3DX12_RESOURCE_DESC::Buffer(kGpuTraditionalCommandStreamCounterBufferBytes);
        const UINT constantsBytes =
            AlignConstantBufferSize(sizeof(GpuTraditionalCommandStreamCullingConstants));
        auto constantsDesc = CD3DX12_RESOURCE_DESC::Buffer(constantsBytes);

        auto createFrameResources = [&](FrameResources& frame) -> bool {
            frame = FrameResources{};

            if (FAILED(device->CreateCommittedResource(
                &argumentHeap,
                D3D12_HEAP_FLAG_NONE,
                &argumentDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.argumentBuffer.GetAddressOf())))) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.argumentBuffer.Get(),
                L"GPU Traditional Command Stream Draw Argument Buffer");

            if (FAILED(device->CreateCommittedResource(
                &argumentHeap,
                D3D12_HEAP_FLAG_NONE,
                &skinnedArgumentDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.skinnedArgumentBuffer.GetAddressOf())))) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.skinnedArgumentBuffer.Get(),
                L"Surface Skinned Indirect Draw Argument Buffer");

            if (FAILED(device->CreateCommittedResource(
                &argumentHeap,
                D3D12_HEAP_FLAG_NONE,
                &seedDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.seedBuffer.GetAddressOf())))) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.seedBuffer.Get(),
                L"GPU Traditional Command Stream Draw Seed Buffer");

            if (FAILED(device->CreateCommittedResource(
                &seedUploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &seedDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(frame.seedUploadBuffer.GetAddressOf())))) {
                return false;
            }
            if (FAILED(frame.seedUploadBuffer->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&frame.seedMapped)))) {
                frame.seedMapped = nullptr;
                return false;
            }
            GFX::SetD3D12Name(
                frame.seedUploadBuffer.Get(),
                L"GPU Traditional Command Stream Draw Seed Upload Buffer");

            if (FAILED(device->CreateCommittedResource(
                &argumentHeap,
                D3D12_HEAP_FLAG_NONE,
                &payloadDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.payloadBuffer.GetAddressOf())))) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.payloadBuffer.Get(),
                L"GPU Traditional Command Stream Draw Payload Buffer");

            if (FAILED(device->CreateCommittedResource(
                &seedUploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &payloadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(frame.payloadUploadBuffer.GetAddressOf())))) {
                return false;
            }
            if (FAILED(frame.payloadUploadBuffer->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&frame.payloadMapped)))) {
                frame.payloadMapped = nullptr;
                return false;
            }
            GFX::SetD3D12Name(
                frame.payloadUploadBuffer.Get(),
                L"GPU Traditional Command Stream Draw Payload Upload Buffer");

            if (FAILED(device->CreateCommittedResource(
                &argumentHeap,
                D3D12_HEAP_FLAG_NONE,
                &counterDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.counterBuffer.GetAddressOf())))) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.counterBuffer.Get(),
                L"GPU Traditional Command Stream Draw Counters");

            if (FAILED(device->CreateCommittedResource(
                &seedUploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &counterUploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(frame.counterResetUploadBuffer.GetAddressOf())))) {
                return false;
            }
            if (FAILED(frame.counterResetUploadBuffer->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&frame.counterResetMapped)))) {
                frame.counterResetMapped = nullptr;
                return false;
            }
            GFX::SetD3D12Name(
                frame.counterResetUploadBuffer.Get(),
                L"GPU Traditional Command Stream Draw Counter Reset");

            if (FAILED(device->CreateCommittedResource(
                &seedUploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &constantsDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(frame.constantsUploadBuffer.GetAddressOf())))) {
                return false;
            }
            if (FAILED(frame.constantsUploadBuffer->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&frame.constantsMapped)))) {
                frame.constantsMapped = nullptr;
                return false;
            }
            GFX::SetD3D12Name(
                frame.constantsUploadBuffer.Get(),
                L"GPU Traditional Command Stream Draw Culling Constants");

            return true;
        };

        for (FrameResources& frame : frameResources_) {
            if (!createFrameResources(frame)) {
                resetGpuBuffers();
                return false;
            }
        }

        if (!CreateComputeRootSignature(device, computeRootSignature_.GetAddressOf()) ||
            !CreateComputePipelineState(
                device,
                computeRootSignature_.Get(),
                compactPipelineState_.GetAddressOf())) {
            resetGpuBuffers();
            computeRootSignature_.Reset();
            compactPipelineState_.Reset();
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[4]{};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
        argumentDescs[0].VertexBuffer.Slot = 0;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
        argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[2].Constant.RootParameterIndex = rootConstantParameterIndex;
        argumentDescs[2].Constant.DestOffsetIn32BitValues = 0;
        argumentDescs[2].Constant.Num32BitValuesToSet = rootConstantCount;
        argumentDescs[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride = static_cast<UINT>(sizeof(GpuTraditionalCommandArgument));
        signatureDesc.NumArgumentDescs = static_cast<UINT>(std::size(argumentDescs));
        signatureDesc.pArgumentDescs = argumentDescs;
        if (FAILED(device->CreateCommandSignature(
            &signatureDesc,
            rootSignature,
            IID_PPV_ARGS(commandSignature_.GetAddressOf())))) {
            commandSignature_.Reset();
            resetGpuBuffers();
            computeRootSignature_.Reset();
            compactPipelineState_.Reset();
            return false;
        }
        GFX::SetD3D12Name(commandSignature_.Get(), L"GPU Traditional Command Stream Draw Command Signature");

        capacity_ = capacity;
        BindFrameResources(0);
        ResetFrame();
        return true;
    }

    bool GpuTraditionalCommandStreamBuffer::InitializeSkinnedCommandStream(
        ID3D12Device* device,
        ID3D12RootSignature* skinnedRootSignature,
        UINT rootConstantParameterIndex,
        UINT jointPaletteParameterIndex) {

        skinnedCommandSignature_.Reset();
        if (device == nullptr ||
            skinnedRootSignature == nullptr ||
            rootConstantCount_ != kGpuTraditionalCommandStreamRootConstantCount ||
            skinnedArgumentBuffer_ == nullptr) {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[5]{};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
        argumentDescs[0].VertexBuffer.Slot = 0;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
        argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[2].Constant.RootParameterIndex = rootConstantParameterIndex;
        argumentDescs[2].Constant.DestOffsetIn32BitValues = 0;
        argumentDescs[2].Constant.Num32BitValuesToSet = rootConstantCount_;
        argumentDescs[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
        argumentDescs[3].ConstantBufferView.RootParameterIndex =
            jointPaletteParameterIndex;
        argumentDescs[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride =
            static_cast<UINT>(sizeof(GpuTraditionalSkinnedCommandArgument));
        signatureDesc.NumArgumentDescs = static_cast<UINT>(std::size(argumentDescs));
        signatureDesc.pArgumentDescs = argumentDescs;

        GFX::ClearD3D12InfoQueue(device);
        const HRESULT hr = device->CreateCommandSignature(
            &signatureDesc,
            skinnedRootSignature,
            IID_PPV_ARGS(skinnedCommandSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(
            hr,
            "GpuTraditionalCommandStream::CreateSkinnedCommandSignature")) {
            GFX::DumpD3D12InfoQueue(
                device,
                "GpuTraditionalCommandStream::CreateSkinnedCommandSignature");
            skinnedCommandSignature_.Reset();
            return false;
        }
        GFX::SetD3D12Name(
            skinnedCommandSignature_.Get(),
            L"Surface Skinned Indirect Draw Command Signature");
        stats_.skinnedCommandSignatureReady = true;
        return true;
    }

    void GpuTraditionalCommandStreamBuffer::BeginFrame(uint32_t frameIndex) {
        StoreActiveFrameResourceStates();
        BindFrameResources(frameIndex);
    }

    void GpuTraditionalCommandStreamBuffer::BindFrameResources(uint32_t frameIndex) {
        activeFrameResourceIndex_ = frameIndex % GFX::kFrameResourceCount;
        FrameResources& frame = frameResources_[activeFrameResourceIndex_];
        argumentBuffer_ = frame.argumentBuffer;
        skinnedArgumentBuffer_ = frame.skinnedArgumentBuffer;
        seedBuffer_ = frame.seedBuffer;
        seedUploadBuffer_ = frame.seedUploadBuffer;
        payloadBuffer_ = frame.payloadBuffer;
        payloadUploadBuffer_ = frame.payloadUploadBuffer;
        counterBuffer_ = frame.counterBuffer;
        counterResetUploadBuffer_ = frame.counterResetUploadBuffer;
        constantsUploadBuffer_ = frame.constantsUploadBuffer;
        seedMapped_ = frame.seedMapped;
        payloadMapped_ = frame.payloadMapped;
        counterResetMapped_ = frame.counterResetMapped;
        constantsMapped_ = frame.constantsMapped;
        argumentBufferState_ = frame.argumentBufferState;
        skinnedArgumentBufferState_ = frame.skinnedArgumentBufferState;
        seedBufferState_ = frame.seedBufferState;
        payloadBufferState_ = frame.payloadBufferState;
        counterBufferState_ = frame.counterBufferState;
    }

    void GpuTraditionalCommandStreamBuffer::StoreActiveFrameResourceStates() {
        FrameResources& frame = frameResources_[activeFrameResourceIndex_];
        frame.argumentBufferState = argumentBufferState_;
        frame.skinnedArgumentBufferState = skinnedArgumentBufferState_;
        frame.seedBufferState = seedBufferState_;
        frame.payloadBufferState = payloadBufferState_;
        frame.counterBufferState = counterBufferState_;
    }

    void GpuTraditionalCommandStreamBuffer::ResetFrame() {
        seedCursor_ = 0;
        payloadCursor_ = 0;
        payloadIndexByGpuSceneInstance_.clear();

        const size_t capacity = capacity_;
        const bool initialized =
            seedMapped_ != nullptr &&
            counterResetMapped_ != nullptr &&
            constantsMapped_ != nullptr &&
            argumentBuffer_ != nullptr &&
            skinnedArgumentBuffer_ != nullptr &&
            seedBuffer_ != nullptr &&
            seedUploadBuffer_ != nullptr &&
            payloadBuffer_ != nullptr &&
            payloadUploadBuffer_ != nullptr &&
            payloadMapped_ != nullptr &&
            counterBuffer_ != nullptr &&
            counterResetUploadBuffer_ != nullptr &&
            constantsUploadBuffer_ != nullptr;
        const bool signatureReady = commandSignature_ != nullptr;
        const bool skinnedSignatureReady = skinnedCommandSignature_ != nullptr;
        const D3D12_GPU_VIRTUAL_ADDRESS address =
            argumentBuffer_ != nullptr ? argumentBuffer_->GetGPUVirtualAddress() : 0;
        const D3D12_GPU_VIRTUAL_ADDRESS skinnedAddress =
            skinnedArgumentBuffer_ != nullptr
                ? skinnedArgumentBuffer_->GetGPUVirtualAddress()
                : 0;
        stats_ = {};
        stats_.capacity = capacity;
        stats_.initialized = initialized;
        stats_.commandSignatureReady = signatureReady;
        stats_.skinnedCommandSignatureReady = skinnedSignatureReady;
        stats_.seedBufferReady = seedBuffer_ != nullptr && seedUploadBuffer_ != nullptr;
        stats_.payloadBufferReady =
            payloadBuffer_ != nullptr && payloadUploadBuffer_ != nullptr;
        stats_.counterBufferReady = counterBuffer_ != nullptr;
        stats_.gpuCompactionPipelineReady =
            computeRootSignature_ != nullptr &&
            compactPipelineState_ != nullptr;
        stats_.gpuCompactedCommandCapacity = capacity;
        stats_.commandBucketCount = kGpuDrivenCommandBucketCount;
        stats_.argumentBufferAddress = address;
        stats_.skinnedArgumentBufferAddress = skinnedAddress;
        stats_.commandStride = static_cast<UINT>(sizeof(GpuTraditionalCommandArgument));
        stats_.skinnedCommandStride =
            static_cast<UINT>(sizeof(GpuTraditionalSkinnedCommandArgument));
    }

    void GpuTraditionalCommandStreamBuffer::UploadCommandSeeds(
        const GpuDrivenTraditionalIndirectView& view) {

        if (view.commands == nullptr || view.commands->empty()) {
            return;
        }

        stats_.requestedCommandCount += view.commands->size();
        ++stats_.uploadCallCount;
        if (seedMapped_ == nullptr ||
            payloadMapped_ == nullptr ||
            capacity_ == 0) {
            stats_.overflowCommandCount += view.commands->size();
            return;
        }

        for (const RUNTIME::SurfaceDrawCommand& command : *view.commands) {
            if (!IsIndirectDrawable(command)) {
                ++stats_.missingDrawArgsCommandCount;
                continue;
            }
            if (seedCursor_ >= capacity_) {
                ++stats_.overflowCommandCount;
                continue;
            }

            const uint32_t absoluteGpuSceneIndex =
                view.gpuSceneBaseIndex + command.firstGpuSceneInstanceIndex;
            const bool skinnedCommand =
                view.jointPalettes != nullptr &&
                command.firstRecordIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
                command.firstRecordIndex < view.jointPalettes->size() &&
                !(*view.jointPalettes)[command.firstRecordIndex].empty();
            if (skinnedCommand && command.jointPaletteGpuAddress == 0) {
                ++stats_.missingJointPaletteCommandCount;
                continue;
            }
            MATH::Vec4 boundsCenterRadius{ 0.0f, 0.0f, 0.0f, -1.0f };
            if (view.instances != nullptr &&
                command.firstGpuSceneInstanceIndex < view.instances->size()) {
                boundsCenterRadius =
                    (*view.instances)[command.firstGpuSceneInstanceIndex].boundsCenterRadius;
            }

            auto* payloads =
                reinterpret_cast<GpuTraditionalCommandPayload*>(payloadMapped_);
            size_t payloadIndex = 0;
            const auto payloadFound =
                payloadIndexByGpuSceneInstance_.find(absoluteGpuSceneIndex);
            if (payloadFound != payloadIndexByGpuSceneInstance_.end()) {
                payloadIndex = payloadFound->second;
                ++stats_.reusedPayloadCount;
            } else {
                if (payloadCursor_ >= capacity_) {
                    ++stats_.overflowCommandCount;
                    continue;
                }
                payloadIndex = payloadCursor_++;
                payloadIndexByGpuSceneInstance_[absoluteGpuSceneIndex] = payloadIndex;
                GpuTraditionalCommandPayload& payload = payloads[payloadIndex];
                payload = ToDrawPayload(command.drawArgs);
                payload.vertexBuffer = command.triangleMeshView.vertexBuffer;
                payload.indexBuffer = command.triangleMeshView.indexBuffer;
                payload.jointPalette = command.jointPaletteGpuAddress;
                payload.flags =
                    command.doubleSided ? kGpuTraditionalCommandStreamSeedFlagDoubleSided : 0u;
                if (skinnedCommand) {
                    payload.flags |= kGpuTraditionalCommandStreamSeedFlagSkinned;
                }
                ++stats_.uploadedPayloadCount;
            }
            GpuTraditionalCommandPayload& payload = payloads[payloadIndex];
            payload.vertexBuffer = command.triangleMeshView.vertexBuffer;
            payload.indexBuffer = command.triangleMeshView.indexBuffer;
            payload.jointPalette = command.jointPaletteGpuAddress;
            if (skinnedCommand) {
                payload.flags |= kGpuTraditionalCommandStreamSeedFlagSkinned;
            }

            auto* seeds = reinterpret_cast<GpuTraditionalCommandSeed*>(seedMapped_);
            GpuTraditionalCommandSeed& seed = seeds[seedCursor_++];
            seed = {};
            seed.boundsCenterRadius = boundsCenterRadius;
            seed.absoluteGpuSceneInstanceIndex = absoluteGpuSceneIndex;
            seed.passIndex =
                static_cast<uint32_t>(ToPassIndex(ResolveCommandPass(command)));
            seed.bucketIndex = ResolveCommandBucketIndex(command);
            seed.payloadIndex = static_cast<uint32_t>(payloadIndex);
            seed.localGpuSceneInstanceIndex = command.firstGpuSceneInstanceIndex;
            seed.flags = command.doubleSided ? kGpuTraditionalCommandStreamSeedFlagDoubleSided : 0u;
            if (skinnedCommand) {
                seed.flags |= kGpuTraditionalCommandStreamSeedFlagSkinned;
                ++stats_.uploadedSkinnedSeedCount;
            } else {
                ++stats_.uploadedStaticSeedCount;
            }

            ++stats_.uploadedCommandCount;
            ++stats_.uploadedSeedCount;
        }
    }

    bool GpuTraditionalCommandStreamBuffer::BuildGpuCompactedCommands(
        ID3D12GraphicsCommandList* commandList,
        const MATH::Mat4& viewProj,
        bool enableFrustumCull) {

        if (commandList == nullptr ||
            seedCursor_ == 0 ||
            capacity_ == 0 ||
            seedBuffer_ == nullptr ||
            seedUploadBuffer_ == nullptr ||
            payloadBuffer_ == nullptr ||
            payloadUploadBuffer_ == nullptr ||
            argumentBuffer_ == nullptr ||
            skinnedArgumentBuffer_ == nullptr ||
            counterBuffer_ == nullptr ||
            counterResetUploadBuffer_ == nullptr ||
            constantsUploadBuffer_ == nullptr ||
            payloadMapped_ == nullptr ||
            constantsMapped_ == nullptr ||
            counterResetMapped_ == nullptr ||
            computeRootSignature_ == nullptr ||
            compactPipelineState_ == nullptr) {
            return false;
        }

        GpuTraditionalCommandStreamCullingConstants constants{};
        constants.viewProj = viewProj;
        constants.inputCount = static_cast<uint32_t>(
            (std::min)(seedCursor_, static_cast<size_t>(UINT32_MAX)));
        constants.outputCapacity = static_cast<uint32_t>(
            (std::min)(capacity_, static_cast<size_t>(UINT32_MAX)));
        constants.enableFrustumCull = enableFrustumCull ? 1u : 0u;
        constants.payloadCount = static_cast<uint32_t>(
            (std::min)(payloadCursor_, static_cast<size_t>(UINT32_MAX)));
        *reinterpret_cast<GpuTraditionalCommandStreamCullingConstants*>(constantsMapped_) =
            constants;

        std::fill(
            counterResetMapped_,
            counterResetMapped_ + kGpuTraditionalCommandStreamCounterBufferBytes,
            std::byte{ 0 });

        if (seedBufferState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                seedBuffer_.Get(),
                seedBufferState_,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &barrier);
            seedBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        if (payloadBufferState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                payloadBuffer_.Get(),
                payloadBufferState_,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &barrier);
            payloadBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        const UINT64 seedBytes =
            static_cast<UINT64>(seedCursor_) *
            static_cast<UINT64>(sizeof(GpuTraditionalCommandSeed));
        commandList->CopyBufferRegion(
            seedBuffer_.Get(),
            0,
            seedUploadBuffer_.Get(),
            0,
            seedBytes);
        const UINT64 payloadBytes =
            static_cast<UINT64>(payloadCursor_) *
            static_cast<UINT64>(sizeof(GpuTraditionalCommandPayload));
        commandList->CopyBufferRegion(
            payloadBuffer_.Get(),
            0,
            payloadUploadBuffer_.Get(),
            0,
            payloadBytes);

        D3D12_RESOURCE_BARRIER preDispatchBarriers[5]{};
        UINT preDispatchBarrierCount = 0;
        preDispatchBarriers[preDispatchBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::Transition(
                seedBuffer_.Get(),
                seedBufferState_,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        seedBufferState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        preDispatchBarriers[preDispatchBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::Transition(
                payloadBuffer_.Get(),
                payloadBufferState_,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        payloadBufferState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (argumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            preDispatchBarriers[preDispatchBarrierCount++] =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    argumentBuffer_.Get(),
                    argumentBufferState_,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            argumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (skinnedArgumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            preDispatchBarriers[preDispatchBarrierCount++] =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    skinnedArgumentBuffer_.Get(),
                    skinnedArgumentBufferState_,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            skinnedArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (counterBufferState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            preDispatchBarriers[preDispatchBarrierCount++] =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    counterBuffer_.Get(),
                    counterBufferState_,
                    D3D12_RESOURCE_STATE_COPY_DEST);
            counterBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        commandList->ResourceBarrier(preDispatchBarrierCount, preDispatchBarriers);

        commandList->CopyBufferRegion(
            counterBuffer_.Get(),
            0,
            counterResetUploadBuffer_.Get(),
            0,
            kGpuTraditionalCommandStreamCounterBufferBytes);
        auto counterToUav = CD3DX12_RESOURCE_BARRIER::Transition(
            counterBuffer_.Get(),
            counterBufferState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->ResourceBarrier(1, &counterToUav);
        counterBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        commandList->SetComputeRootSignature(computeRootSignature_.Get());
        commandList->SetPipelineState(compactPipelineState_.Get());
        commandList->SetComputeRootConstantBufferView(
            0,
            constantsUploadBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(
            1,
            seedBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            2,
            argumentBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(
            3,
            payloadBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            4,
            skinnedArgumentBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            5,
            counterBuffer_->GetGPUVirtualAddress());

        const UINT groupCount =
            static_cast<UINT>(
                (seedCursor_ + kGpuTraditionalCommandStreamThreadGroupSize - 1u) /
                kGpuTraditionalCommandStreamThreadGroupSize);
        commandList->Dispatch(groupCount, 1u, 1u);

        D3D12_RESOURCE_BARRIER uavBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(argumentBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(skinnedArgumentBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(counterBuffer_.Get()),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(uavBarriers)),
            uavBarriers);

        D3D12_RESOURCE_BARRIER readyBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                argumentBuffer_.Get(),
                argumentBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
            CD3DX12_RESOURCE_BARRIER::Transition(
                skinnedArgumentBuffer_.Get(),
                skinnedArgumentBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
            CD3DX12_RESOURCE_BARRIER::Transition(
                counterBuffer_.Get(),
                counterBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(readyBarriers)),
            readyBarriers);
        argumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        skinnedArgumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        counterBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

        stats_.gpuBuildDispatchCount = 1u;
        stats_.gpuCompactionReady = true;
        stats_.gpuCounterBacked = true;
        StoreActiveFrameResourceStates();
        return true;
    }

    ID3D12Resource* GpuTraditionalCommandStreamBuffer::GetArgumentBuffer() const {
        return argumentBuffer_.Get();
    }

    ID3D12Resource* GpuTraditionalCommandStreamBuffer::GetSkinnedArgumentBuffer() const {
        return skinnedArgumentBuffer_.Get();
    }

    ID3D12Resource* GpuTraditionalCommandStreamBuffer::GetCounterBuffer() const {
        return counterBuffer_.Get();
    }

    ID3D12CommandSignature* GpuTraditionalCommandStreamBuffer::GetCommandSignature() const {
        return commandSignature_.Get();
    }

    ID3D12CommandSignature* GpuTraditionalCommandStreamBuffer::GetSkinnedCommandSignature() const {
        return skinnedCommandSignature_.Get();
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetCommandCounterOffset() const {
        return 0u;
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetCommandCounterOffset(GpuDrivenPassKind pass) const {
        return GetCommandCounterOffset(
            pass,
            GpuDrivenCommandBucket::BackFaceCulled);
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetCommandCounterOffset(
        GpuDrivenPassKind pass,
        GpuDrivenCommandBucket bucket) const {

        const UINT64 passIndex = static_cast<UINT64>(ToPassIndex(pass));
        const UINT64 bucketIndex =
            static_cast<UINT64>(ToCommandBucketIndex(bucket));
        return (passIndex * static_cast<UINT64>(kGpuDrivenCommandBucketCount) +
            bucketIndex) *
            static_cast<UINT64>(kGpuTraditionalCommandStreamCounterStrideBytes);
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetSkinnedCommandCounterOffset() const {
        return 16u;
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetSkinnedCommandCounterOffset(GpuDrivenPassKind pass) const {
        return GetSkinnedCommandCounterOffset(
            pass,
            GpuDrivenCommandBucket::BackFaceCulled);
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetSkinnedCommandCounterOffset(
        GpuDrivenPassKind pass,
        GpuDrivenCommandBucket bucket) const {

        return GetCommandCounterOffset(pass, bucket) + 16u;
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetArgumentBufferOffset(
        GpuDrivenPassKind pass) const {

        return GetArgumentBufferOffset(
            pass,
            GpuDrivenCommandBucket::BackFaceCulled);
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetArgumentBufferOffset(
        GpuDrivenPassKind pass,
        GpuDrivenCommandBucket bucket) const {

        const UINT64 passIndex = static_cast<UINT64>(ToPassIndex(pass));
        const UINT64 bucketIndex =
            static_cast<UINT64>(ToCommandBucketIndex(bucket));
        return (passIndex * static_cast<UINT64>(kGpuDrivenCommandBucketCount) +
            bucketIndex) *
            static_cast<UINT64>(capacity_) *
            static_cast<UINT64>(sizeof(GpuTraditionalCommandArgument));
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetSkinnedArgumentBufferOffset(
        GpuDrivenPassKind pass) const {

        return GetSkinnedArgumentBufferOffset(
            pass,
            GpuDrivenCommandBucket::BackFaceCulled);
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetSkinnedArgumentBufferOffset(
        GpuDrivenPassKind pass,
        GpuDrivenCommandBucket bucket) const {

        const UINT64 passIndex = static_cast<UINT64>(ToPassIndex(pass));
        const UINT64 bucketIndex =
            static_cast<UINT64>(ToCommandBucketIndex(bucket));
        return (passIndex * static_cast<UINT64>(kGpuDrivenCommandBucketCount) +
            bucketIndex) *
            static_cast<UINT64>(capacity_) *
            static_cast<UINT64>(sizeof(GpuTraditionalSkinnedCommandArgument));
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetArgumentBucketStride() const {
        return static_cast<UINT64>(capacity_) *
            static_cast<UINT64>(sizeof(GpuTraditionalCommandArgument));
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetSkinnedArgumentBucketStride() const {
        return static_cast<UINT64>(capacity_) *
            static_cast<UINT64>(sizeof(GpuTraditionalSkinnedCommandArgument));
    }

    UINT64 GpuTraditionalCommandStreamBuffer::GetCounterBucketStride() const {
        return kGpuTraditionalCommandStreamCounterStrideBytes;
    }

    size_t GpuTraditionalCommandStreamBuffer::GetCommandBucketCapacity() const {
        return capacity_;
    }

    size_t GpuTraditionalCommandStreamBuffer::GetCommandBucketCount() const {
        return kGpuDrivenCommandBucketCount;
    }

    size_t GpuTraditionalCommandStreamBuffer::GetUploadedSeedCount() const {
        return seedCursor_;
    }

    size_t GpuTraditionalCommandStreamBuffer::GetUploadedSkinnedSeedCount() const {
        return stats_.uploadedSkinnedSeedCount;
    }

    bool GpuTraditionalCommandStreamBuffer::HasGpuCompactedCommands() const {
        const bool hasStaticStream =
            stats_.uploadedStaticSeedCount != 0 &&
            argumentBuffer_ != nullptr &&
            commandSignature_ != nullptr;
        const bool hasSkinnedStream =
            stats_.uploadedSkinnedSeedCount != 0 &&
            skinnedArgumentBuffer_ != nullptr &&
            skinnedCommandSignature_ != nullptr;
        return stats_.gpuCompactionReady &&
            stats_.gpuCounterBacked &&
            stats_.uploadedSeedCount != 0 &&
            counterBuffer_ != nullptr &&
            (hasStaticStream || hasSkinnedStream);
    }

    const GpuTraditionalCommandStreamStats& GpuTraditionalCommandStreamBuffer::GetStats() const {
        return stats_;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
