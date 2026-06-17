#pragma once

#include <cstddef>

#include <d3d12.h>

#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenClusterWorkContext {
        CLUSTER::ClusterGpuCullingPass* cullingPass = nullptr;
        ID3D12GraphicsCommandList* commandList = nullptr;
        MATH::Mat4 viewProj{};
        MATH::Vec3 cameraPosition{};
        D3D12_GPU_DESCRIPTOR_HANDLE clusterGeometryPoolSrv{};
        D3D12_GPU_VIRTUAL_ADDRESS surfaceGpuSceneGpuAddress = 0;
        const GpuDrivenFrame* frame = nullptr;
    };

    struct GpuDrivenClusterWorkResult {
        bool submitted = false;
        size_t sourcePassCount = 0;
        size_t sourceInstanceCount = 0;
        const CLUSTER::ClusterGpuCullingPassStats* stats = nullptr;
    };

    CLUSTER::ClusterGpuCullingPassKind ToClusterCullPassKind(
        GpuDrivenPassKind passKind);

    GpuDrivenClusterWorkResult BuildGpuDrivenClusterWork(
        const GpuDrivenClusterWorkContext& context);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
