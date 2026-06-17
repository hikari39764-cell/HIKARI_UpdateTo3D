#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    constexpr size_t kDefaultSurfaceIndirectDrawCommandCapacity = 4096u;
    constexpr UINT kSurfaceIndirectRootConstantCount = 4u;
    using SurfaceIndirectCommandFilter =
        bool (*)(const RUNTIME::SurfaceDrawCommand& command, const void* userData);

    struct SurfaceIndirectDrawArgument {
        D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
        D3D12_INDEX_BUFFER_VIEW indexBuffer{};
        uint32_t rootConstants[kSurfaceIndirectRootConstantCount]{};
        D3D12_DRAW_INDEXED_ARGUMENTS draw{};
    };

    static_assert(sizeof(SurfaceIndirectDrawArgument) == 72u);

    struct SurfaceIndirectDrawBufferStats {
        size_t capacity = 0;
        size_t requestedCommandCount = 0;
        size_t uploadedCommandCount = 0;
        size_t overflowCommandCount = 0;
        size_t filteredCommandCount = 0;
        size_t cpuDirectCommandCount = 0;
        size_t missingDrawArgsCommandCount = 0;
        size_t drawBindingPatchCount = 0;
        size_t uploadCallCount = 0;
        bool initialized = false;
        bool commandSignatureReady = false;
        D3D12_GPU_VIRTUAL_ADDRESS argumentBufferAddress = 0;
        UINT commandStride = 0;
    };

    class SurfaceIndirectDrawBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount,
            size_t capacity = kDefaultSurfaceIndirectDrawCommandCapacity);

        void ResetFrame();
        void UploadSurfaceCommands(
            const std::vector<RUNTIME::SurfaceDrawCommand>& commands,
            uint32_t rootBaseOffset,
            SurfaceIndirectCommandFilter filter = nullptr,
            const void* filterUserData = nullptr);
        bool FlushToGpu(ID3D12GraphicsCommandList* commandList);

        ID3D12Resource* GetArgumentBuffer() const;
        ID3D12CommandSignature* GetCommandSignature() const;
        bool TryGetArgumentBufferOffset(
            const RUNTIME::SurfaceDrawCommand& command,
            UINT64& outOffsetBytes) const;
        bool PatchDrawBinding(
            const RUNTIME::SurfaceDrawCommand& command,
            const D3D12_VERTEX_BUFFER_VIEW& vertexBuffer,
            const D3D12_INDEX_BUFFER_VIEW& indexBuffer);
        bool HasDrawBinding(const RUNTIME::SurfaceDrawCommand& command) const;
        const SurfaceIndirectDrawBufferStats& GetStats() const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> argumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> commandSignature_;
        SurfaceIndirectDrawArgument* mapped_ = nullptr;
        size_t capacity_ = 0;
        size_t cursor_ = 0;
        UINT rootConstantCount_ = 0;
        std::unordered_map<const RUNTIME::SurfaceDrawCommand*, size_t> argumentIndexByCommand_{};
        std::unordered_map<const RUNTIME::SurfaceDrawCommand*, bool> drawBindingByCommand_{};
        SurfaceIndirectDrawBufferStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
