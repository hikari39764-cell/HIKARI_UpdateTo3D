#pragma once

#include <cstddef>

#include <d3d12.h>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenCommandLayout {
        size_t drawArgumentBucketCapacity = 0;
        UINT64 backFaceDrawArgumentOffset = 0;
        UINT64 doubleSidedDrawArgumentOffset = 0;
        UINT64 backFaceCounterOffset = 0;
        UINT64 doubleSidedCounterOffset = 0;
        UINT64 meshletBackFaceDispatchOffset = 0;
        UINT64 meshletDoubleSidedDispatchOffset = 0;
    };

    struct GpuCommandBuildResult {
        ID3D12Resource* drawIndexedArgs = nullptr;
        ID3D12Resource* clusterDrawArgs = nullptr;
        ID3D12Resource* meshletDispatchArgs = nullptr;

        ID3D12CommandSignature* drawIndexedSignature = nullptr;
        ID3D12CommandSignature* clusterDrawSignature = nullptr;
        ID3D12CommandSignature* meshletDispatchSignature = nullptr;

        GpuDrivenCommandLayout layout{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
