#pragma once

#include <array>
#include <cstddef>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuVisibilityBucketResult {
        size_t sourceInstanceCount = 0;
    };

    struct GpuVisibilityResult {
        ID3D12Resource* visibleInstanceBuffer = nullptr;
        ID3D12Resource* visibleClusterRangeBuffer = nullptr;
        ID3D12Resource* visibleMeshletRangeBuffer = nullptr;
        ID3D12Resource* counterBuffer = nullptr;

        std::array<GpuVisibilityBucketResult, kGpuDrivenCommandBucketCount> buckets{};
        size_t submittedDrawSeedCount = 0;

        size_t GetSourceInstanceCount(GpuDrivenCommandBucket bucket) const {
            return buckets[ToCommandBucketIndex(bucket)].sourceInstanceCount;
        }

        bool HasKnownBucketSourceCounts() const {
            size_t total = 0;
            for (const GpuVisibilityBucketResult& bucket : buckets) {
                total += bucket.sourceInstanceCount;
            }
            return total != 0;
        }

        bool HasSourceForBucket(GpuDrivenCommandBucket bucket) const {
            if (!HasKnownBucketSourceCounts()) {
                return true;
            }
            return GetSourceInstanceCount(bucket) != 0;
        }
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
