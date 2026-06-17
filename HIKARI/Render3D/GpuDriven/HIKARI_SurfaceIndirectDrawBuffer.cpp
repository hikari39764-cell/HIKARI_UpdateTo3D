#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"

#include <algorithm>
#include <iterator>

#include <d3dx12.h>

#include "Gfx/HIKARI_DXCheck.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
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
        commandSignature_.Reset();
        mapped_ = nullptr;
        capacity_ = 0;
        cursor_ = 0;
        rootConstantCount_ = rootConstantCount;
        argumentIndexByCommand_.clear();
        drawBindingByCommand_.clear();
        stats_ = {};

        const UINT64 bufferBytes =
            static_cast<UINT64>(sizeof(SurfaceIndirectDrawArgument)) *
            static_cast<UINT64>(capacity);
        auto argumentHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferBytes);
        if (FAILED(device->CreateCommittedResource(
            &argumentHeap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
            nullptr,
            IID_PPV_ARGS(argumentBuffer_.GetAddressOf())))) {
            return false;
        }
        GFX::SetD3D12Name(argumentBuffer_.Get(), L"Surface Indirect Draw Argument Buffer");

        auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        if (FAILED(device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
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
        argumentIndexByCommand_.clear();
        drawBindingByCommand_.clear();

        const size_t capacity = capacity_;
        const bool initialized =
            mapped_ != nullptr &&
            argumentBuffer_ != nullptr &&
            uploadBuffer_ != nullptr;
        const bool signatureReady = commandSignature_ != nullptr;
        const D3D12_GPU_VIRTUAL_ADDRESS address =
            argumentBuffer_ != nullptr ? argumentBuffer_->GetGPUVirtualAddress() : 0;
        stats_ = {};
        stats_.capacity = capacity;
        stats_.initialized = initialized;
        stats_.commandSignatureReady = signatureReady;
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
            argumentIndexByCommand_[&command] = argumentIndex;
            drawBindingByCommand_[&command] = false;
            ++stats_.uploadedCommandCount;
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
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
            D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->ResourceBarrier(1, &toCopy);
        commandList->CopyBufferRegion(
            argumentBuffer_.Get(),
            0,
            uploadBuffer_.Get(),
            0,
            copyBytes);
        auto toIndirect = CD3DX12_RESOURCE_BARRIER::Transition(
            argumentBuffer_.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        commandList->ResourceBarrier(1, &toIndirect);
        return true;
    }

    ID3D12Resource* SurfaceIndirectDrawBuffer::GetArgumentBuffer() const {
        return argumentBuffer_.Get();
    }

    ID3D12CommandSignature* SurfaceIndirectDrawBuffer::GetCommandSignature() const {
        return commandSignature_.Get();
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
