#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"
#include "Render3D/GpuDriven/HIKARI_GeometryBackendContext.h"

namespace HIKARI::RENDER3D::CLUSTER {

    enum class ClusterDrawPipelineKind : uint32_t {
        ForwardOpaque,
        GeometryAux,
    };

    struct ClusterDrawExecutorStats {
        bool drawArgumentBufferReady = false;
        bool drawCommandSignatureReady = false;
        bool drawPipelineReady = false;
        bool forwardPipelineReady = false;
        bool geometryAuxPipelineReady = false;
        size_t requestedDrawCount = 0;
        size_t submittedDrawCount = 0;
        size_t skippedDrawCount = 0;
        size_t skippedBucketCount = 0;
        size_t submitCallCount = 0;
        size_t forwardSubmittedDrawCount = 0;
        size_t geometryAuxSubmittedDrawCount = 0;
        size_t forwardSubmitCallCount = 0;
        size_t geometryAuxSubmitCallCount = 0;
        size_t backFaceSubmitCallCount = 0;
        size_t doubleSidedSubmitCallCount = 0;
    };

    struct ClusterDrawExecutionContext {
        ID3D12GraphicsCommandList* commandList = nullptr;
        const GPUDRIVEN::GpuVisibilityResult* visibility = nullptr;
        const GPUDRIVEN::GpuCommandBuildResult* commands = nullptr;
        ClusterDrawPipelineKind pipelineKind = ClusterDrawPipelineKind::ForwardOpaque;
    };

    class ClusterDrawExecutor final {
    public:
        using PipelineBucketArray =
            std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, GPUDRIVEN::kGpuDrivenCommandBucketCount>;

        bool Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
        void Reset();
        void ResetFrame();
        bool Execute(const ClusterDrawExecutionContext& ctx);

        const ClusterDrawExecutorStats& GetStats() const;
        ID3D12PipelineState* GetPipelineState() const;
        ID3D12PipelineState* GetPipelineState(ClusterDrawPipelineKind kind) const;
        ID3D12PipelineState* GetPipelineState(
            ClusterDrawPipelineKind kind,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) const;

    private:
        PipelineBucketArray forwardPipelineStates_{};
        PipelineBucketArray geometryAuxPipelineStates_{};
        ClusterDrawExecutorStats stats_{};
    };

} // namespace HIKARI::RENDER3D::CLUSTER
