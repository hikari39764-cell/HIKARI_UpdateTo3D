#pragma once

#include <cstddef>

#include <d3d12.h>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuVisibilityResult {
        ID3D12Resource* visibleInstanceBuffer = nullptr;
        ID3D12Resource* visibleClusterRangeBuffer = nullptr;
        ID3D12Resource* visibleMeshletRangeBuffer = nullptr;
        ID3D12Resource* counterBuffer = nullptr;

        size_t sourceSingleSidedInstanceCount = 0;
        size_t sourceDoubleSidedInstanceCount = 0;
        size_t submittedDrawSeedCount = 0;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
