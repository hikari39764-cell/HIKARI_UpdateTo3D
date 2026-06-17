#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/GpuDriven/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrameContext.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    class SurfaceGpuSceneFrameBuffer;
    class SurfaceIndirectDrawBuffer;
    struct GpuDrivenSceneSource;

    class GpuDrivenLayer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* staticRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount);

        void Attach(
            SurfaceGpuSceneFrameBuffer* sceneBuffer,
            SurfaceIndirectDrawBuffer* indirectDrawBuffer,
            CLUSTER::ClusterGpuCullingPass* clusterCullingPass);

        void ResetFrame();
        bool BeginFrame(const GpuDrivenSceneSource* source);
        void UploadSurfaceGpuSceneFrame(
            uint32_t instanceCount,
            uint32_t opaqueBaseIndex,
            uint32_t depthAwareBaseIndex,
            uint32_t transparentBaseIndex,
            bool sceneResident);
        void PrepareSurfaceGpuSceneMaterialFrame();
        void DispatchVisibility(
            const CLUSTER::ClusterGpuCullingPassStats& stats);
        void BuildCommandBuffers();

        GeometryBackendContext BuildGeometryBackendContext(
            ID3D12GraphicsCommandList* commandList,
            GpuDrivenPassKind pass,
            GeometryBackendKind backend) const;

        SurfaceGpuSceneFrameBuffer* GetSceneBuffer() const;
        SurfaceIndirectDrawBuffer* GetIndirectDrawBuffer() const;
        CLUSTER::ClusterGpuCullingPass* GetClusterCullingPass() const;
        const GpuDrivenFrameContext& GetFrameContext() const;

    private:
        SurfaceGpuSceneFrameBuffer* sceneBuffer_ = nullptr;
        SurfaceIndirectDrawBuffer* indirectDrawBuffer_ = nullptr;
        CLUSTER::ClusterGpuCullingPass* clusterCullingPass_ = nullptr;
        GpuDrivenFrameContext frameContext_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
