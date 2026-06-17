#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    constexpr size_t kDefaultSurfaceGpuSceneInstanceCapacity = 8192u;

    struct SurfaceGpuSceneFrameBufferStats {
        size_t capacity = 0;
        size_t requestedInstanceCount = 0;
        size_t uploadedInstanceCount = 0;
        size_t overflowInstanceCount = 0;
        size_t uploadCallCount = 0;
        bool initialized = false;
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    };

    class SurfaceGpuSceneFrameBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpu,
            size_t capacity = kDefaultSurfaceGpuSceneInstanceCapacity);

        void ResetFrame();
        void ReuseFrame(size_t residentInstanceCount);
        void Upload(const RUNTIME::SurfaceGpuSceneInstance* instances, size_t count);
        void Upload(const std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances);
        bool UpdateRange(
            size_t firstInstance,
            const RUNTIME::SurfaceGpuSceneInstance* instances,
            size_t count);
        bool PatchMaterialDataIndex(size_t instanceIndex, uint32_t materialDataIndex);
        bool HasMaterialDataIndex(size_t instanceIndex) const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetSrv() const;
        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
        const SurfaceGpuSceneFrameBufferStats& GetStats() const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
        RUNTIME::SurfaceGpuSceneInstance* mapped_ = nullptr;
        size_t capacity_ = 0;
        size_t cursor_ = 0;
        SurfaceGpuSceneFrameBufferStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
