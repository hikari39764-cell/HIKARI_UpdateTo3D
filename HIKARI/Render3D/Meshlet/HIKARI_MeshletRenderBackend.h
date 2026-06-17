#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/GpuDriven/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"

namespace HIKARI::RENDER3D::MESHLET {

    enum class MeshletPipelineKind : uint32_t {
        ForwardOpaque,
        GeometryAux,
    };

    struct MeshletRenderBackendStats {
        bool initialized = false;
        bool shaderModel65Supported = false;
        bool meshShaderSupported = false;
        bool meshShaderPipelineStatsSupported = false;
        bool shaderCompileReady = false;
        bool dispatchArgumentBufferReady = false;
        bool dispatchCommandSignatureReady = false;
        bool forwardPipelineReady = false;
        bool geometryAuxPipelineReady = false;
        bool pipelineReady = false;
        uint32_t meshShaderTier = 0;
        size_t pipelineCreateRequestCount = 0;
        size_t pipelineCreateReadyCount = 0;
        size_t requestedDispatchCount = 0;
        size_t submittedDispatchCount = 0;
        size_t skippedDispatchCount = 0;
        size_t submitCallCount = 0;
        size_t skippedBucketCount = 0;
        size_t forwardSubmittedDispatchCount = 0;
        size_t geometryAuxSubmittedDispatchCount = 0;
        size_t backFaceSubmitCallCount = 0;
        size_t doubleSidedSubmitCallCount = 0;
    };

    struct MeshletRenderExecutionContext {
        ID3D12GraphicsCommandList* commandList = nullptr;
        GPUDRIVEN::GpuDrivenPassKind pass = GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque;
        const GPUDRIVEN::GpuVisibilityResult* visibility = nullptr;
        const GPUDRIVEN::GpuCommandBuildResult* commands = nullptr;
        MeshletPipelineKind pipelineKind = MeshletPipelineKind::ForwardOpaque;
    };

    class MeshletRenderBackend final {
    public:
        using PipelineBucketArray =
            std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, GPUDRIVEN::kGpuDrivenCommandBucketCount>;

        bool Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
        void Reset();
        void ResetFrame();
        bool Execute(const MeshletRenderExecutionContext& ctx);

        const MeshletRenderBackendStats& GetStats() const;
        ID3D12PipelineState* GetPipelineState(
            MeshletPipelineKind kind,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) const;

    private:
        PipelineBucketArray forwardPipelineStates_{};
        PipelineBucketArray geometryAuxPipelineStates_{};
        MeshletRenderBackendStats stats_{};
    };

} // namespace HIKARI::RENDER3D::MESHLET
