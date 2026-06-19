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
        ForwardDepthAware,
        ForwardTransparent,
        Shadow,
        GeometryAux,
    };

    namespace ClusterDrawPipelineMask {
        constexpr uint32_t ForwardOpaque = 1u << 0;
        constexpr uint32_t ForwardDepthAware = 1u << 1;
        constexpr uint32_t ForwardTransparent = 1u << 2;
        constexpr uint32_t Shadow = 1u << 3;
        constexpr uint32_t GeometryAux = 1u << 4;
        constexpr uint32_t MainRenderer =
            ForwardOpaque |
            ForwardDepthAware |
            ForwardTransparent |
            GeometryAux;
        constexpr uint32_t ShadowRenderer = Shadow;
        constexpr uint32_t All = MainRenderer | ShadowRenderer;
    }

    struct ClusterDrawExecutorStats {
        bool drawArgumentBufferReady = false;
        bool drawCommandSignatureReady = false;
        bool drawPipelineReady = false;
        bool forwardPipelineReady = false;
        bool depthAwarePipelineReady = false;
        bool transparentPipelineReady = false;
        bool shadowPipelineReady = false;
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
        GPUDRIVEN::GpuDrivenPassKind pass = GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque;
        const GPUDRIVEN::GpuVisibilityResult* visibility = nullptr;
        const GPUDRIVEN::GpuDrivenDrawCommandRange* drawCommandRange = nullptr;
        ClusterDrawPipelineKind pipelineKind = ClusterDrawPipelineKind::ForwardOpaque;
    };

    class ClusterDrawExecutor final {
    public:
        using PipelineBucketArray =
            std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, GPUDRIVEN::kGpuDrivenCommandBucketCount>;

        bool Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            uint32_t pipelineMask);
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
        PipelineBucketArray depthAwarePipelineStates_{};
        PipelineBucketArray transparentPipelineStates_{};
        PipelineBucketArray shadowPipelineStates_{};
        PipelineBucketArray geometryAuxPipelineStates_{};
        uint32_t pipelineMask_ = ClusterDrawPipelineMask::All;
        ClusterDrawExecutorStats stats_{};
    };

} // namespace HIKARI::RENDER3D::CLUSTER
