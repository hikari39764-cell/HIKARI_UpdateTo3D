#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"

#include <algorithm>
#include <iterator>

#include <d3dx12.h>

#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        constexpr uint32_t kSurfaceIndirectThreadGroupSize = 64u;
        constexpr UINT kSurfaceIndirectCounterBufferBytes = 16u;

        constexpr UINT AlignConstantBufferSize(size_t size) {
            return static_cast<UINT>((size + 255u) & ~255u);
        }

        D3D12_DRAW_INDEXED_ARGUMENTS ToD3D12DrawArgs(
            const RUNTIME::SurfaceDrawIndexedArgs& args) {

            D3D12_DRAW_INDEXED_ARGUMENTS draw{};
            draw.IndexCountPerInstance = args.indexCountPerInstance;
            draw.InstanceCount = args.instanceCount;
            draw.StartIndexLocation = args.startIndexLocation;
            draw.BaseVertexLocation = args.baseVertexLocation;
            draw.StartInstanceLocation = args.startInstanceLocation;
            return draw;
        }

        bool IsIndirectDrawable(const RUNTIME::SurfaceDrawCommand& command) {
            return
                command.backend == RUNTIME::SurfaceDrawCommandBackend::GpuDriven &&
                command.drawArgsValid &&
                command.drawArgs.indexCountPerInstance > 0 &&
                command.drawArgs.instanceCount > 0 &&
                command.firstGpuSceneInstanceIndex != RUNTIME::kInvalidRenderSurfaceIndex;
        }

        struct SurfaceIndirectDrawSeed {
            SurfaceIndirectDrawArgument argument{};
            MATH::Vec4 boundsCenterRadius{};
            uint32_t absoluteGpuSceneInstanceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t flags = 0;
            uint32_t reserved0 = 0;
            uint32_t reserved1 = 0;
        };

        static_assert(sizeof(SurfaceIndirectDrawSeed) == 104u);

        struct SurfaceIndirectCullingConstants {
            MATH::Mat4 viewProj{};
            uint32_t inputCount = 0;
            uint32_t outputCapacity = 0;
            uint32_t enableFrustumCull = 1;
            uint32_t reserved0 = 0;
        };

        bool CreateComputeRootSignature(
            ID3D12Device* device,
            ID3D12RootSignature** outRootSignature) {

            if (device == nullptr || outRootSignature == nullptr) {
                return false;
            }

            D3D12_ROOT_PARAMETER params[4]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[1].Descriptor.ShaderRegister = 0;

            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[2].Descriptor.ShaderRegister = 0;

            params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[3].Descriptor.ShaderRegister = 1;

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
            if (!HIKARI_DX_CHECK(hr, "SurfaceIndirectDraw::CreateComputeRootSignature")) {
                return false;
            }
            GFX::SetD3D12Name(*outRootSignature, L"Surface Indirect GPU Culling Root Signature");
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
                L"HIKARI/Shaders/Render3D_SurfaceIndirectCullCS.hlsl",
                "CompactSurfaceIndirectCS",
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
            if (!HIKARI_DX_CHECK(hr, "SurfaceIndirectDraw::CreateComputePipelineState")) {
                return false;
            }
            GFX::SetD3D12Name(*outPipelineState, L"Surface Indirect GPU Compact PSO");
            return true;
        }
    }

    bool SurfaceIndirectDrawBuffer::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount,
        size_t capacity) {

        if (device == nullptr ||
            rootSignature == nullptr ||
            rootConstantCount == 0 ||
            rootConstantCount != kSurfaceIndirectRootConstantCount ||
            capacity == 0) {
            return false;
        }

        argumentBuffer_.Reset();
        uploadBuffer_.Reset();
        seedBuffer_.Reset();
        seedUploadBuffer_.Reset();
        counterBuffer_.Reset();
        counterResetUploadBuffer_.Reset();
        constantsUploadBuffer_.Reset();
        computeRootSignature_.Reset();
        compactPipelineState_.Reset();
        commandSignature_.Reset();
        mapped_ = nullptr;
        seedMapped_ = nullptr;
        counterResetMapped_ = nullptr;
        constantsMapped_ = nullptr;
        capacity_ = 0;
        cursor_ = 0;
        seedCursor_ = 0;
        rootConstantCount_ = rootConstantCount;
        argumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        seedBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        counterBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        argumentIndexByCommand_.clear();
        drawBindingByCommand_.clear();
        stats_ = {};

        const UINT64 bufferBytes =
            static_cast<UINT64>(sizeof(SurfaceIndirectDrawArgument)) *
            static_cast<UINT64>(capacity);
        auto argumentHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto argumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            bufferBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (FAILED(device->CreateCommittedResource(
            &argumentHeap,
            D3D12_HEAP_FLAG_NONE,
            &argumentDesc,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
            nullptr,
            IID_PPV_ARGS(argumentBuffer_.GetAddressOf())))) {
            return false;
        }
        GFX::SetD3D12Name(argumentBuffer_.Get(), L"Surface Indirect Draw Argument Buffer");

        auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferBytes);
        auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        if (FAILED(device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(uploadBuffer_.GetAddressOf())))) {
            argumentBuffer_.Reset();
            return false;
        }
        if (FAILED(uploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped_)))) {
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        GFX::SetD3D12Name(uploadBuffer_.Get(), L"Surface Indirect Draw Upload Buffer");

        const UINT64 seedBytes =
            static_cast<UINT64>(sizeof(SurfaceIndirectDrawSeed)) *
            static_cast<UINT64>(capacity);
        auto seedDesc = CD3DX12_RESOURCE_DESC::Buffer(seedBytes);
        if (FAILED(device->CreateCommittedResource(
            &argumentHeap,
            D3D12_HEAP_FLAG_NONE,
            &seedDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(seedBuffer_.GetAddressOf())))) {
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        GFX::SetD3D12Name(seedBuffer_.Get(), L"Surface Indirect Draw Seed Buffer");

        auto seedUploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        if (FAILED(device->CreateCommittedResource(
            &seedUploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &seedDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(seedUploadBuffer_.GetAddressOf())))) {
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        if (FAILED(seedUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&seedMapped_)))) {
            seedMapped_ = nullptr;
            seedUploadBuffer_.Reset();
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        GFX::SetD3D12Name(seedUploadBuffer_.Get(), L"Surface Indirect Draw Seed Upload Buffer");

        auto counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
            kSurfaceIndirectCounterBufferBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (FAILED(device->CreateCommittedResource(
            &argumentHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(counterBuffer_.GetAddressOf())))) {
            seedMapped_ = nullptr;
            seedUploadBuffer_.Reset();
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        GFX::SetD3D12Name(counterBuffer_.Get(), L"Surface Indirect Draw Counters");

        auto counterUploadDesc =
            CD3DX12_RESOURCE_DESC::Buffer(kSurfaceIndirectCounterBufferBytes);
        if (FAILED(device->CreateCommittedResource(
            &seedUploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterUploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(counterResetUploadBuffer_.GetAddressOf())))) {
            counterBuffer_.Reset();
            seedMapped_ = nullptr;
            seedUploadBuffer_.Reset();
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        if (FAILED(counterResetUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&counterResetMapped_)))) {
            counterResetMapped_ = nullptr;
            counterResetUploadBuffer_.Reset();
            counterBuffer_.Reset();
            seedMapped_ = nullptr;
            seedUploadBuffer_.Reset();
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        GFX::SetD3D12Name(counterResetUploadBuffer_.Get(), L"Surface Indirect Draw Counter Reset");

        const UINT constantsBytes =
            AlignConstantBufferSize(sizeof(SurfaceIndirectCullingConstants));
        auto constantsDesc = CD3DX12_RESOURCE_DESC::Buffer(constantsBytes);
        if (FAILED(device->CreateCommittedResource(
            &seedUploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &constantsDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(constantsUploadBuffer_.GetAddressOf())))) {
            counterResetMapped_ = nullptr;
            counterResetUploadBuffer_.Reset();
            counterBuffer_.Reset();
            seedMapped_ = nullptr;
            seedUploadBuffer_.Reset();
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        if (FAILED(constantsUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantsMapped_)))) {
            constantsMapped_ = nullptr;
            constantsUploadBuffer_.Reset();
            counterResetMapped_ = nullptr;
            counterResetUploadBuffer_.Reset();
            counterBuffer_.Reset();
            seedMapped_ = nullptr;
            seedUploadBuffer_.Reset();
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        GFX::SetD3D12Name(constantsUploadBuffer_.Get(), L"Surface Indirect Draw Culling Constants");

        if (!CreateComputeRootSignature(device, computeRootSignature_.GetAddressOf()) ||
            !CreateComputePipelineState(
                device,
                computeRootSignature_.Get(),
                compactPipelineState_.GetAddressOf())) {
            constantsMapped_ = nullptr;
            constantsUploadBuffer_.Reset();
            counterResetMapped_ = nullptr;
            counterResetUploadBuffer_.Reset();
            counterBuffer_.Reset();
            seedMapped_ = nullptr;
            seedUploadBuffer_.Reset();
            seedBuffer_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
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
        signatureDesc.ByteStride = static_cast<UINT>(sizeof(SurfaceIndirectDrawArgument));
        signatureDesc.NumArgumentDescs = static_cast<UINT>(std::size(argumentDescs));
        signatureDesc.pArgumentDescs = argumentDescs;
        if (FAILED(device->CreateCommandSignature(
            &signatureDesc,
            rootSignature,
            IID_PPV_ARGS(commandSignature_.GetAddressOf())))) {
            commandSignature_.Reset();
            mapped_ = nullptr;
            uploadBuffer_.Reset();
            argumentBuffer_.Reset();
            return false;
        }
        GFX::SetD3D12Name(commandSignature_.Get(), L"Surface Indirect Draw Command Signature");

        capacity_ = capacity;
        ResetFrame();
        return true;
    }

    void SurfaceIndirectDrawBuffer::ResetFrame() {
        cursor_ = 0;
        seedCursor_ = 0;
        argumentIndexByCommand_.clear();
        drawBindingByCommand_.clear();

        const size_t capacity = capacity_;
        const bool initialized =
            mapped_ != nullptr &&
            seedMapped_ != nullptr &&
            counterResetMapped_ != nullptr &&
            constantsMapped_ != nullptr &&
            argumentBuffer_ != nullptr &&
            uploadBuffer_ != nullptr &&
            seedBuffer_ != nullptr &&
            seedUploadBuffer_ != nullptr &&
            counterBuffer_ != nullptr &&
            counterResetUploadBuffer_ != nullptr &&
            constantsUploadBuffer_ != nullptr;
        const bool signatureReady = commandSignature_ != nullptr;
        const D3D12_GPU_VIRTUAL_ADDRESS address =
            argumentBuffer_ != nullptr ? argumentBuffer_->GetGPUVirtualAddress() : 0;
        stats_ = {};
        stats_.capacity = capacity;
        stats_.initialized = initialized;
        stats_.commandSignatureReady = signatureReady;
        stats_.seedBufferReady = seedBuffer_ != nullptr && seedUploadBuffer_ != nullptr;
        stats_.counterBufferReady = counterBuffer_ != nullptr;
        stats_.gpuCompactionPipelineReady =
            computeRootSignature_ != nullptr &&
            compactPipelineState_ != nullptr;
        stats_.gpuCompactedCommandCapacity = capacity;
        stats_.argumentBufferAddress = address;
        stats_.commandStride = static_cast<UINT>(sizeof(SurfaceIndirectDrawArgument));
    }

    void SurfaceIndirectDrawBuffer::UploadSurfaceCommands(
        const std::vector<RUNTIME::SurfaceDrawCommand>& commands,
        uint32_t rootBaseOffset,
        SurfaceIndirectCommandFilter filter,
        const void* filterUserData) {

        stats_.requestedCommandCount += commands.size();
        ++stats_.uploadCallCount;
        if (commands.empty()) {
            return;
        }
        if (mapped_ == nullptr || capacity_ == 0) {
            stats_.overflowCommandCount += commands.size();
            return;
        }

        for (const RUNTIME::SurfaceDrawCommand& command : commands) {
            if (filter != nullptr && !filter(command, filterUserData)) {
                ++stats_.filteredCommandCount;
                continue;
            }
            if (command.backend != RUNTIME::SurfaceDrawCommandBackend::GpuDriven) {
                ++stats_.cpuDirectCommandCount;
                continue;
            }
            if (!IsIndirectDrawable(command)) {
                ++stats_.missingDrawArgsCommandCount;
                continue;
            }
            if (cursor_ >= capacity_) {
                ++stats_.overflowCommandCount;
                continue;
            }

            const size_t argumentIndex = cursor_;
            SurfaceIndirectDrawArgument& dst = mapped_[cursor_++];
            dst = {};
            // command signature と構造体の draw offset を固定するため、root constants は常に 4 DWORD にする。
            // root constants[0] は pass ごとのインスタンス基点として扱う。
            // Forward は SurfaceGpuScene、Shadow は ShadowObjectData の base index として読む。
            dst.rootConstants[0] = rootBaseOffset + command.firstGpuSceneInstanceIndex;
            dst.rootConstants[1] = 1u;
            dst.rootConstants[2] = 0u;
            dst.rootConstants[3] = 0u;
            dst.draw = ToD3D12DrawArgs(command.drawArgs);
            if (seedMapped_ != nullptr && seedCursor_ < capacity_) {
                auto* seeds =
                    reinterpret_cast<SurfaceIndirectDrawSeed*>(seedMapped_);
                SurfaceIndirectDrawSeed& seed = seeds[seedCursor_++];
                seed = {};
                seed.argument = dst;
                seed.absoluteGpuSceneInstanceIndex =
                    rootBaseOffset + command.firstGpuSceneInstanceIndex;
                seed.boundsCenterRadius = { 0.0f, 0.0f, 0.0f, -1.0f };
                ++stats_.uploadedSeedCount;
            }
            argumentIndexByCommand_[&command] = argumentIndex;
            drawBindingByCommand_[&command] = false;
            ++stats_.uploadedCommandCount;
        }
    }

    void SurfaceIndirectDrawBuffer::UploadSurfaceCommandSeeds(
        const GpuDrivenTraditionalIndirectView& view,
        SurfaceIndirectCommandFilter filter,
        const void* filterUserData) {

        if (view.commands == nullptr || view.commands->empty()) {
            return;
        }

        stats_.requestedCommandCount += view.commands->size();
        ++stats_.uploadCallCount;
        if (seedMapped_ == nullptr || mapped_ == nullptr || capacity_ == 0) {
            stats_.overflowCommandCount += view.commands->size();
            return;
        }

        for (const RUNTIME::SurfaceDrawCommand& command : *view.commands) {
            if (filter != nullptr && !filter(command, filterUserData)) {
                ++stats_.filteredCommandCount;
                continue;
            }
            if (command.backend != RUNTIME::SurfaceDrawCommandBackend::GpuDriven) {
                ++stats_.cpuDirectCommandCount;
                continue;
            }
            if (!IsIndirectDrawable(command)) {
                ++stats_.missingDrawArgsCommandCount;
                continue;
            }
            if (cursor_ >= capacity_ || seedCursor_ >= capacity_) {
                ++stats_.overflowCommandCount;
                continue;
            }

            const uint32_t absoluteGpuSceneIndex =
                view.gpuSceneBaseIndex + command.firstGpuSceneInstanceIndex;
            MATH::Vec4 boundsCenterRadius{ 0.0f, 0.0f, 0.0f, -1.0f };
            if (view.instances != nullptr &&
                command.firstGpuSceneInstanceIndex < view.instances->size()) {
                boundsCenterRadius =
                    (*view.instances)[command.firstGpuSceneInstanceIndex].boundsCenterRadius;
            }

            const size_t argumentIndex = cursor_;
            SurfaceIndirectDrawArgument& argument = mapped_[cursor_++];
            argument = {};
            argument.rootConstants[0] = absoluteGpuSceneIndex;
            argument.rootConstants[1] = 1u;
            argument.rootConstants[2] = 0u;
            argument.rootConstants[3] = 0u;
            argument.draw = ToD3D12DrawArgs(command.drawArgs);

            auto* seeds = reinterpret_cast<SurfaceIndirectDrawSeed*>(seedMapped_);
            SurfaceIndirectDrawSeed& seed = seeds[seedCursor_++];
            seed = {};
            seed.argument = argument;
            seed.boundsCenterRadius = boundsCenterRadius;
            seed.absoluteGpuSceneInstanceIndex = absoluteGpuSceneIndex;
            seed.flags = command.doubleSided ? 1u : 0u;

            argumentIndexByCommand_[&command] = argumentIndex;
            drawBindingByCommand_[&command] = false;
            ++stats_.uploadedCommandCount;
            ++stats_.uploadedSeedCount;
        }
    }

    bool SurfaceIndirectDrawBuffer::FlushToGpu(ID3D12GraphicsCommandList* commandList) {
        if (commandList == nullptr ||
            argumentBuffer_ == nullptr ||
            uploadBuffer_ == nullptr ||
            stats_.uploadedCommandCount == 0) {
            return false;
        }

        const UINT64 copyBytes =
            static_cast<UINT64>(stats_.uploadedCommandCount) *
            static_cast<UINT64>(sizeof(SurfaceIndirectDrawArgument));
        auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            argumentBuffer_.Get(),
            argumentBufferState_,
            D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->ResourceBarrier(1, &toCopy);
        argumentBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        commandList->CopyBufferRegion(
            argumentBuffer_.Get(),
            0,
            uploadBuffer_.Get(),
            0,
            copyBytes);
        auto toIndirect = CD3DX12_RESOURCE_BARRIER::Transition(
            argumentBuffer_.Get(),
            argumentBufferState_,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        commandList->ResourceBarrier(1, &toIndirect);
        argumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        return true;
    }

    bool SurfaceIndirectDrawBuffer::BuildGpuCompactedCommands(
        ID3D12GraphicsCommandList* commandList,
        const MATH::Mat4& viewProj,
        bool enableFrustumCull) {

        if (commandList == nullptr ||
            seedCursor_ == 0 ||
            capacity_ == 0 ||
            seedBuffer_ == nullptr ||
            seedUploadBuffer_ == nullptr ||
            argumentBuffer_ == nullptr ||
            counterBuffer_ == nullptr ||
            counterResetUploadBuffer_ == nullptr ||
            constantsUploadBuffer_ == nullptr ||
            constantsMapped_ == nullptr ||
            counterResetMapped_ == nullptr ||
            computeRootSignature_ == nullptr ||
            compactPipelineState_ == nullptr) {
            return false;
        }

        SurfaceIndirectCullingConstants constants{};
        constants.viewProj = viewProj;
        constants.inputCount = static_cast<uint32_t>(
            (std::min)(seedCursor_, static_cast<size_t>(UINT32_MAX)));
        constants.outputCapacity = static_cast<uint32_t>(
            (std::min)(capacity_, static_cast<size_t>(UINT32_MAX)));
        constants.enableFrustumCull = enableFrustumCull ? 1u : 0u;
        *reinterpret_cast<SurfaceIndirectCullingConstants*>(constantsMapped_) =
            constants;

        std::fill(
            counterResetMapped_,
            counterResetMapped_ + kSurfaceIndirectCounterBufferBytes,
            std::byte{ 0 });

        if (seedBufferState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                seedBuffer_.Get(),
                seedBufferState_,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &barrier);
            seedBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        const UINT64 seedBytes =
            static_cast<UINT64>(seedCursor_) *
            static_cast<UINT64>(sizeof(SurfaceIndirectDrawSeed));
        commandList->CopyBufferRegion(
            seedBuffer_.Get(),
            0,
            seedUploadBuffer_.Get(),
            0,
            seedBytes);

        D3D12_RESOURCE_BARRIER preDispatchBarriers[3]{};
        UINT preDispatchBarrierCount = 0;
        preDispatchBarriers[preDispatchBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::Transition(
                seedBuffer_.Get(),
                seedBufferState_,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        seedBufferState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (argumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            preDispatchBarriers[preDispatchBarrierCount++] =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    argumentBuffer_.Get(),
                    argumentBufferState_,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            argumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
            kSurfaceIndirectCounterBufferBytes);
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
        commandList->SetComputeRootUnorderedAccessView(
            3,
            counterBuffer_->GetGPUVirtualAddress());

        const UINT groupCount =
            static_cast<UINT>(
                (seedCursor_ + kSurfaceIndirectThreadGroupSize - 1u) /
                kSurfaceIndirectThreadGroupSize);
        commandList->Dispatch(groupCount, 1u, 1u);

        D3D12_RESOURCE_BARRIER uavBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(argumentBuffer_.Get()),
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
                counterBuffer_.Get(),
                counterBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(readyBarriers)),
            readyBarriers);
        argumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        counterBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

        stats_.gpuBuildDispatchCount = 1u;
        stats_.gpuCompactionReady = true;
        stats_.gpuCounterBacked = true;
        return true;
    }

    ID3D12Resource* SurfaceIndirectDrawBuffer::GetArgumentBuffer() const {
        return argumentBuffer_.Get();
    }

    ID3D12Resource* SurfaceIndirectDrawBuffer::GetCounterBuffer() const {
        return counterBuffer_.Get();
    }

    ID3D12CommandSignature* SurfaceIndirectDrawBuffer::GetCommandSignature() const {
        return commandSignature_.Get();
    }

    UINT64 SurfaceIndirectDrawBuffer::GetCommandCounterOffset() const {
        return 0u;
    }

    size_t SurfaceIndirectDrawBuffer::GetUploadedSeedCount() const {
        return seedCursor_;
    }

    bool SurfaceIndirectDrawBuffer::HasGpuCompactedCommands() const {
        return stats_.gpuCompactionReady &&
            stats_.gpuCounterBacked &&
            stats_.uploadedSeedCount != 0 &&
            argumentBuffer_ != nullptr &&
            counterBuffer_ != nullptr &&
            commandSignature_ != nullptr;
    }

    bool SurfaceIndirectDrawBuffer::TryGetArgumentBufferOffset(
        const RUNTIME::SurfaceDrawCommand& command,
        UINT64& outOffsetBytes) const {

        const auto found = argumentIndexByCommand_.find(&command);
        if (found == argumentIndexByCommand_.end()) {
            return false;
        }

        outOffsetBytes =
            static_cast<UINT64>(found->second) *
            static_cast<UINT64>(sizeof(SurfaceIndirectDrawArgument));
        return true;
    }

    bool SurfaceIndirectDrawBuffer::PatchDrawBinding(
        const RUNTIME::SurfaceDrawCommand& command,
        const D3D12_VERTEX_BUFFER_VIEW& vertexBuffer,
        const D3D12_INDEX_BUFFER_VIEW& indexBuffer) {

        if (mapped_ == nullptr) {
            return false;
        }

        const auto found = argumentIndexByCommand_.find(&command);
        if (found == argumentIndexByCommand_.end() || found->second >= capacity_) {
            return false;
        }

        SurfaceIndirectDrawArgument& argument = mapped_[found->second];
        argument.vertexBuffer = vertexBuffer;
        argument.indexBuffer = indexBuffer;
        if (seedMapped_ != nullptr && found->second < seedCursor_) {
            auto* seeds = reinterpret_cast<SurfaceIndirectDrawSeed*>(seedMapped_);
            seeds[found->second].argument.vertexBuffer = vertexBuffer;
            seeds[found->second].argument.indexBuffer = indexBuffer;
        }
        drawBindingByCommand_[&command] = true;
        ++stats_.drawBindingPatchCount;
        return true;
    }

    bool SurfaceIndirectDrawBuffer::HasDrawBinding(
        const RUNTIME::SurfaceDrawCommand& command) const {

        const auto found = drawBindingByCommand_.find(&command);
        return found != drawBindingByCommand_.end() && found->second;
    }

    const SurfaceIndirectDrawBufferStats& SurfaceIndirectDrawBuffer::GetStats() const {
        return stats_;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
