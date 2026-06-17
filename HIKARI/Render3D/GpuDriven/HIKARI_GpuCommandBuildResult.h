#pragma once

#include <array>
#include <cstddef>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenCommandBucketLayout {
        UINT64 gpuDrawIndexedArgumentOffset = 0;
        UINT64 meshDispatchArgumentOffset = 0;
        UINT64 counterOffset = 0;
    };

    struct GpuDrivenCommandLayout {
        size_t commandBucketCapacity = 0;
        std::array<GpuDrivenCommandBucketLayout, kGpuDrivenCommandBucketCount> buckets{};

        const GpuDrivenCommandBucketLayout& GetBucket(
            GpuDrivenCommandBucket bucket) const {

            return buckets[ToCommandBucketIndex(bucket)];
        }

        GpuDrivenCommandBucketLayout& GetBucket(
            GpuDrivenCommandBucket bucket) {

            return buckets[ToCommandBucketIndex(bucket)];
        }
    };

    struct GpuCommandBuildResult {
        ID3D12Resource* surfaceDrawIndexedArgs = nullptr;
        ID3D12Resource* gpuDrawIndexedArgs = nullptr;
        ID3D12Resource* meshDispatchArgs = nullptr;

        ID3D12CommandSignature* surfaceDrawIndexedSignature = nullptr;
        ID3D12CommandSignature* gpuDrawIndexedSignature = nullptr;
        ID3D12CommandSignature* meshDispatchSignature = nullptr;

        GpuDrivenCommandLayout layout{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
