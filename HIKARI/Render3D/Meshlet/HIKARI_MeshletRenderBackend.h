#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::RENDER3D::CLUSTER {
    class ClusterGpuCullingPass;
}

namespace HIKARI::RENDER3D::MESHLET {

    enum class MeshletPipelineKind : uint32_t {
        ForwardOpaque,
        GeometryBuffer,
    };

    enum class MeshletCullModeBucket : uint32_t {
        BackFace,
        DoubleSided,
    };

    constexpr size_t kMeshletCullModeBucketCount = 2u;

    struct MeshletRenderBackendStats {
        bool initialized = false;
        bool shaderModel65Supported = false;
        bool meshShaderSupported = false;
        bool meshShaderPipelineStatsSupported = false;
        bool shaderCompileReady = false;
        bool dispatchArgumentBufferReady = false;
        bool dispatchCommandSignatureReady = false;
        bool forwardPipelineReady = false;
        bool geometryBufferPipelineReady = false;
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
        size_t geometryBufferSubmittedDispatchCount = 0;
        size_t backFaceSubmitCallCount = 0;
        size_t doubleSidedSubmitCallCount = 0;
    };

    struct MeshletRenderExecutionContext {
        ID3D12GraphicsCommandList* commandList = nullptr;
        const CLUSTER::ClusterGpuCullingPass* cullingPass = nullptr;
        MeshletPipelineKind pipelineKind = MeshletPipelineKind::ForwardOpaque;
    };

    class MeshletRenderBackend final {
    public:
        using PipelineBucketArray =
            std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kMeshletCullModeBucketCount>;

        bool Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
        void Reset();
        void ResetFrame();
        bool Execute(const MeshletRenderExecutionContext& ctx);

        const MeshletRenderBackendStats& GetStats() const;
        ID3D12PipelineState* GetPipelineState(
            MeshletPipelineKind kind,
            MeshletCullModeBucket bucket) const;

    private:
        PipelineBucketArray forwardPipelineStates_{};
        PipelineBucketArray geometryBufferPipelineStates_{};
        MeshletRenderBackendStats stats_{};
    };

} // namespace HIKARI::RENDER3D::MESHLET
