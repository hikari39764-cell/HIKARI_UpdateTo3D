#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenTraditionalIndirectView;

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

    struct SurfaceSkinnedIndirectDrawArgument {
        D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
        D3D12_INDEX_BUFFER_VIEW indexBuffer{};
        uint32_t rootConstants[kSurfaceIndirectRootConstantCount]{};
        D3D12_GPU_VIRTUAL_ADDRESS jointPalette = 0;
        D3D12_DRAW_INDEXED_ARGUMENTS draw{};
        uint32_t reserved0 = 0;
    };

    static_assert(sizeof(SurfaceSkinnedIndirectDrawArgument) == 80u);

    struct SurfaceIndirectDrawBufferStats {
        size_t capacity = 0;
        size_t requestedCommandCount = 0;
        size_t uploadedCommandCount = 0;
        size_t uploadedStaticSeedCount = 0;
        size_t uploadedSkinnedSeedCount = 0;
        size_t overflowCommandCount = 0;
        size_t filteredCommandCount = 0;
        size_t cpuDirectCommandCount = 0;
        size_t missingDrawArgsCommandCount = 0;
        size_t drawBindingPatchCount = 0;
        size_t skinnedDrawBindingPatchCount = 0;
        size_t missingJointPaletteCommandCount = 0;
        size_t uploadCallCount = 0;
        size_t uploadedSeedCount = 0;
        size_t gpuBuildDispatchCount = 0;
        size_t gpuCompactedCommandCapacity = 0;
        bool initialized = false;
        bool commandSignatureReady = false;
        bool skinnedCommandSignatureReady = false;
        bool seedBufferReady = false;
        bool counterBufferReady = false;
        bool gpuCompactionPipelineReady = false;
        bool gpuCompactionReady = false;
        bool gpuCounterBacked = false;
        D3D12_GPU_VIRTUAL_ADDRESS argumentBufferAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS skinnedArgumentBufferAddress = 0;
        UINT commandStride = 0;
        UINT skinnedCommandStride = 0;
    };

    class SurfaceIndirectDrawBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount,
            size_t capacity = kDefaultSurfaceIndirectDrawCommandCapacity);
        bool InitializeSkinnedCommandStream(
            ID3D12Device* device,
            ID3D12RootSignature* skinnedRootSignature,
            UINT rootConstantParameterIndex,
            UINT jointPaletteParameterIndex);

        void ResetFrame();
        void UploadSurfaceCommands(
            const std::vector<RUNTIME::SurfaceDrawCommand>& commands,
            uint32_t rootBaseOffset,
            SurfaceIndirectCommandFilter filter = nullptr,
            const void* filterUserData = nullptr);
        void UploadSurfaceCommandSeeds(
            const GpuDrivenTraditionalIndirectView& view,
            SurfaceIndirectCommandFilter filter = nullptr,
            const void* filterUserData = nullptr);
        bool FlushToGpu(ID3D12GraphicsCommandList* commandList);
        bool BuildGpuCompactedCommands(
            ID3D12GraphicsCommandList* commandList,
            const MATH::Mat4& viewProj,
            bool enableFrustumCull = true);

        ID3D12Resource* GetArgumentBuffer() const;
        ID3D12Resource* GetSkinnedArgumentBuffer() const;
        ID3D12Resource* GetCounterBuffer() const;
        ID3D12CommandSignature* GetCommandSignature() const;
        ID3D12CommandSignature* GetSkinnedCommandSignature() const;
        UINT64 GetCommandCounterOffset() const;
        UINT64 GetCommandCounterOffset(GpuDrivenPassKind pass) const;
        UINT64 GetSkinnedCommandCounterOffset() const;
        UINT64 GetSkinnedCommandCounterOffset(GpuDrivenPassKind pass) const;
        UINT64 GetArgumentBufferOffset(GpuDrivenPassKind pass) const;
        UINT64 GetSkinnedArgumentBufferOffset(GpuDrivenPassKind pass) const;
        size_t GetUploadedSeedCount() const;
        size_t GetUploadedSkinnedSeedCount() const;
        bool HasGpuCompactedCommands() const;
        bool TryGetArgumentBufferOffset(
            const RUNTIME::SurfaceDrawCommand& command,
            UINT64& outOffsetBytes) const;
        bool PatchDrawBinding(
            const RUNTIME::SurfaceDrawCommand& command,
            const D3D12_VERTEX_BUFFER_VIEW& vertexBuffer,
            const D3D12_INDEX_BUFFER_VIEW& indexBuffer);
        bool PatchSkinnedDrawBinding(
            const RUNTIME::SurfaceDrawCommand& command,
            const D3D12_VERTEX_BUFFER_VIEW& vertexBuffer,
            const D3D12_INDEX_BUFFER_VIEW& indexBuffer,
            D3D12_GPU_VIRTUAL_ADDRESS jointPalette);
        bool HasDrawBinding(const RUNTIME::SurfaceDrawCommand& command) const;
        const SurfaceIndirectDrawBufferStats& GetStats() const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> argumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> skinnedArgumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> seedBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> seedUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> counterBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> counterResetUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> constantsUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> compactPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> commandSignature_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> skinnedCommandSignature_;
        SurfaceIndirectDrawArgument* mapped_ = nullptr;
        SurfaceSkinnedIndirectDrawArgument* skinnedMapped_ = nullptr;
        std::byte* seedMapped_ = nullptr;
        std::byte* counterResetMapped_ = nullptr;
        std::byte* constantsMapped_ = nullptr;
        size_t capacity_ = 0;
        size_t cursor_ = 0;
        size_t seedCursor_ = 0;
        UINT rootConstantCount_ = 0;
        D3D12_RESOURCE_STATES argumentBufferState_ =
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        D3D12_RESOURCE_STATES skinnedArgumentBufferState_ =
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        D3D12_RESOURCE_STATES seedBufferState_ =
            D3D12_RESOURCE_STATE_COPY_DEST;
        D3D12_RESOURCE_STATES counterBufferState_ =
            D3D12_RESOURCE_STATE_COPY_DEST;
        std::unordered_map<const RUNTIME::SurfaceDrawCommand*, size_t> argumentIndexByCommand_{};
        std::unordered_map<const RUNTIME::SurfaceDrawCommand*, bool> drawBindingByCommand_{};
        SurfaceIndirectDrawBufferStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
