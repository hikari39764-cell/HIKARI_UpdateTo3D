#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::CLUSTER {

    constexpr size_t kDefaultClusterGpuCullingVisibleRangeCapacity = 262144u;
    constexpr size_t kDefaultClusterGpuDrawArgumentCapacity = 262144u;
    constexpr size_t kDefaultClusterGpuPageTaskCapacity = 262144u;

    enum class ClusterDrawCullModeBucket : uint32_t {
        BackFace = 0,
        DoubleSided = 1,
        Count = 2,
    };

    constexpr size_t kClusterDrawCullModeBucketCount =
        static_cast<size_t>(ClusterDrawCullModeBucket::Count);
    constexpr UINT64 kClusterGpuCullDrawCommandCounterOffsetBytes = 16u;
    constexpr UINT64 kClusterGpuCullDoubleSidedDrawCommandCounterOffsetBytes = 20u;

    enum class ClusterGpuCullingPassKind : uint32_t {
        ForwardOpaque = 0,
        ForwardDepthAware = 1,
        ForwardTransparent = 2,
        Shadow = 3,
    };

    struct ClusterGpuCullingSourceRange {
        uint32_t surfaceGpuSceneBaseIndex = 0;
        uint32_t instanceCount = 0;
        ClusterGpuCullingPassKind passKind = ClusterGpuCullingPassKind::ForwardOpaque;
        uint32_t singleSidedInstanceCount = 0;
        uint32_t doubleSidedInstanceCount = 0;
    };

    struct ClusterGpuCullingPassStats {
        bool initialized = false;
        bool psoReady = false;
        bool inputBufferReady = false;
        bool pageTaskBufferReady = false;
        bool visibleRangeBufferReady = false;
        bool drawArgumentBufferReady = false;
        bool dispatchArgumentBufferReady = false;
        bool drawCommandSignatureReady = false;
        bool counterBufferReady = false;
        size_t inputCapacity = 0;
        size_t pageTaskCapacity = 0;
        size_t visibleRangeCapacity = 0;
        size_t drawArgumentCapacity = 0;
        size_t sourceInstanceCount = 0;
        size_t sourceSingleSidedInstanceCount = 0;
        size_t sourceDoubleSidedInstanceCount = 0;
        size_t candidateInstanceCount = 0;
        size_t submittedInstanceCount = 0;
        size_t sourcePageTaskCount = 0;
        size_t submittedPageTaskCount = 0;
        size_t submittedDrawSeedCount = 0;
        size_t overflowInstanceCount = 0;
        bool gpuCounterReadbackReady = false;
        bool gpuCounterReadbackValid = false;
        bool debugCountersEnabled = false;
        uint32_t gpuInputCount = 0;
        uint32_t gpuPageTaskCount = 0;
        uint32_t gpuPageTaskOverflowCount = 0;
        uint32_t gpuVisibleRangeCount = 0;
        uint32_t gpuVisibleClusterCount = 0;
        uint32_t gpuOverflowCount = 0;
        uint32_t gpuInputFrustumCulledCount = 0;
        uint32_t gpuPageTestedCount = 0;
        uint32_t gpuPageFrustumCulledCount = 0;
        uint32_t gpuClusterTestedCount = 0;
        uint32_t gpuClusterFrustumCulledCount = 0;
        uint32_t gpuClusterConeCulledCount = 0;
        uint32_t gpuClusterConeTestedCount = 0;
        uint32_t gpuDoubleSidedClusterCount = 0;
        uint32_t gpuDrawCommandCount = 0;
        uint32_t gpuBackFaceDrawCommandCount = 0;
        uint32_t gpuDoubleSidedDrawCommandCount = 0;
        uint32_t gpuDrawCommandOverflowCount = 0;
        uint32_t gpuBackFaceDrawCommandOverflowCount = 0;
        uint32_t gpuDoubleSidedDrawCommandOverflowCount = 0;
        uint32_t gpuMergedGapCount = 0;
        uint32_t gpuMergedGapIndexCount = 0;
        size_t dispatchCount = 0;
        size_t workgroupCount = 0;
        uint32_t threadGroupSize = 64;
    };

    class ClusterGpuCullingPass final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* drawRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount,
            size_t pageTaskCapacity = kDefaultClusterGpuPageTaskCapacity,
            size_t visibleRangeCapacity = kDefaultClusterGpuCullingVisibleRangeCapacity,
            size_t drawArgumentCapacity = kDefaultClusterGpuDrawArgumentCapacity);
        void Reset();

        bool Dispatch(
            ID3D12GraphicsCommandList* commandList,
            const MATH::Mat4& viewProj,
            const MATH::Vec3& cameraPosition,
            D3D12_GPU_DESCRIPTOR_HANDLE clusterGeometryPoolSrv,
            D3D12_GPU_VIRTUAL_ADDRESS surfaceGpuSceneGpuAddress,
            const ClusterGpuCullingSourceRange* ranges,
            size_t rangeCount);

        const ClusterGpuCullingPassStats& GetStats() const;
        ID3D12Resource* GetVisibleRangeBuffer() const;
        ID3D12Resource* GetDrawArgumentBuffer() const;
        ID3D12Resource* GetMeshletDispatchArgumentBuffer() const;
        ID3D12Resource* GetCounterBuffer() const;
        ID3D12CommandSignature* GetDrawCommandSignature() const;
        ID3D12CommandSignature* GetMeshletDispatchCommandSignature() const;
        size_t GetDrawArgumentBucketCapacity() const;
        UINT64 GetDrawArgumentBufferOffset(ClusterDrawCullModeBucket bucket) const;
        UINT64 GetMeshletDispatchArgumentBufferOffset(ClusterDrawCullModeBucket bucket) const;
        UINT64 GetDrawCommandCounterOffset(ClusterDrawCullModeBucket bucket) const;

    private:
        struct GpuVisibleRange {
            uint32_t gpuSceneInstanceIndex = 0;
            uint32_t clusterGeometrySrvDescriptorIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t firstCluster = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t clusterCount = 0;
            uint32_t clusterSurfaceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t passKind = 0;
            uint32_t flags = 0;
            uint32_t clusterIndex = RUNTIME::kInvalidRenderSurfaceIndex;
        };

        static_assert(sizeof(GpuVisibleRange) == 32u);

        struct GpuIndirectDrawArgument {
            uint32_t rootConstants[4]{};
            D3D12_DRAW_ARGUMENTS draw{};
        };

        static_assert(sizeof(GpuIndirectDrawArgument) == 32u);

        struct GpuIndirectMeshletDispatchArgument {
            uint32_t rootConstants[4]{};
            D3D12_DISPATCH_MESH_ARGUMENTS dispatch{};
            uint32_t reserved0 = 0;
        };

        static_assert(sizeof(GpuIndirectMeshletDispatchArgument) == 32u);

        struct GpuPageTask {
            MATH::Mat4 clusterWorld{};
            MATH::Vec4 boundsCenterRadius{};
            uint32_t gpuSceneInstanceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t clusterGeometrySrvDescriptorIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t firstCluster = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t endCluster = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t clusterSurfaceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t passKind = 0;
            uint32_t flags = 0;
            uint32_t pageIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t reserved0 = 0;
            uint32_t reserved1 = 0;
            uint32_t reserved2 = 0;
            uint32_t reserved3 = 0;
        };

        static_assert(sizeof(GpuPageTask) == 128u);

        struct GpuCounters {
            uint32_t inputCount = 0;
            uint32_t visibleRangeCount = 0;
            uint32_t visibleClusterCount = 0;
            uint32_t overflowCount = 0;
            uint32_t backFaceDrawCommandCount = 0;
            uint32_t doubleSidedDrawCommandCount = 0;
            uint32_t backFaceDrawCommandOverflowCount = 0;
            uint32_t doubleSidedDrawCommandOverflowCount = 0;
            uint32_t inputFrustumCulledCount = 0;
            uint32_t pageTestedCount = 0;
            uint32_t pageFrustumCulledCount = 0;
            uint32_t clusterTestedCount = 0;
            uint32_t clusterFrustumCulledCount = 0;
            uint32_t clusterConeCulledCount = 0;
            uint32_t clusterConeTestedCount = 0;
            uint32_t doubleSidedClusterCount = 0;
            uint32_t pageTaskCount = 0;
            uint32_t pageTaskOverflowCount = 0;
            uint32_t mergedGapCount = 0;
            uint32_t mergedGapIndexCount = 0;
        };

        static_assert(sizeof(GpuCounters) == 80u);

        struct CounterReadbackSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
            bool resolved = false;
        };

        struct GpuConstants {
            MATH::Mat4 viewProj{};
            MATH::Vec4 cameraPosition{};
            uint32_t inputCount = 0;
            uint32_t visibleRangeCapacity = 0;
            uint32_t drawArgumentCapacity = 0;
            uint32_t enableFrustumCull = 1;
            uint32_t drawArgumentBucketCapacity = 0;
            uint32_t clusterSrvPoolBegin = 0;
            uint32_t clusterSrvPoolCount = 0;
            uint32_t enableConeCull = 1;
            uint32_t enableDebugCounters = 0;
            uint32_t surfaceGpuSceneBaseIndex = 0;
            uint32_t passKind = 0;
            uint32_t pageTaskCapacity = 0;
            uint32_t mergeGapIndexLimit = 0;
            uint32_t mergeRunGapIndexBudget = 0;
            uint32_t mergeMaxIndexSpan = 0;
            uint32_t mergeClusterGapLimit = 0;
            float lodTargetErrorNdc = 0.0f;
            uint32_t enableLodErrorSelection = 0;
            uint32_t reserved0 = 0;
            uint32_t reserved1 = 0;
        };

        static_assert(sizeof(GpuConstants) == 160u);

        bool EnsurePipeline(ID3D12Device* device);
        bool EnsureDispatchCommandSignature(ID3D12Device* device);
        bool EnsureDrawCommandSignature(
            ID3D12Device* device,
            ID3D12RootSignature* drawRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount);
        bool EnsureMeshletDispatchCommandSignature(
            ID3D12Device* device,
            ID3D12RootSignature* drawRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount);
        bool EnsureCapacity(
            ID3D12Device* device,
            size_t pageTaskCapacity,
            size_t visibleRangeCapacity,
            size_t drawArgumentCapacity);
        void ResetFrameStats();
        void BuildRangeStats(const ClusterGpuCullingSourceRange* ranges, size_t rangeCount);
        void CollectCounterReadback(CounterReadbackSlot& slot);
        void QueueCounterReadback(ID3D12GraphicsCommandList* commandList);

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> expandPageTasksPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> finalizeDispatchPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> finalizeMeshletDispatchPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> cullPageTasksPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatchCommandSignature_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawCommandSignature_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> meshletDispatchCommandSignature_;
        Microsoft::WRL::ComPtr<ID3D12Resource> constantsUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> counterResetUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> pageTaskBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> visibleRangeBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> drawArgumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> meshletDispatchArgumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> dispatchArgumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> counterBuffer_;
        std::array<CounterReadbackSlot, 3> counterReadbackSlots_{};

        GpuConstants* constantsMapped_ = nullptr;
        GpuCounters* counterResetMapped_ = nullptr;
        GpuCounters latestGpuCounters_{};
        size_t pageTaskCapacity_ = 0;
        size_t visibleRangeCapacity_ = 0;
        size_t drawArgumentCapacity_ = 0;
        size_t counterReadbackWriteIndex_ = 0;
        bool latestGpuCountersValid_ = false;
        D3D12_RESOURCE_STATES pageTaskBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES visibleRangeBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES drawArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES counterBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        ClusterGpuCullingPassStats stats_{};
    };

} // namespace HIKARI::RENDER3D::CLUSTER
