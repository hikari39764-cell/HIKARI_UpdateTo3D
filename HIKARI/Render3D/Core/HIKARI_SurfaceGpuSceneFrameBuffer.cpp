#include "Render3D/Core/HIKARI_SurfaceGpuSceneFrameBuffer.h"

#include <algorithm>
#include <cstring>

#include <d3dx12.h>

#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::CORE {

    namespace {
        D3D12_SHADER_RESOURCE_VIEW_DESC BuildSurfaceGpuSceneSrvDesc(size_t elementCount) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = static_cast<UINT>(elementCount > 0 ? elementCount : 1);
            srvDesc.Buffer.StructureByteStride = sizeof(RUNTIME::SurfaceGpuSceneInstance);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            return srvDesc;
        }
    }

    bool SurfaceGpuSceneFrameBuffer::Initialize(
        ID3D12Device* device,
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu,
        size_t capacity) {

        if (device == nullptr || srvCpu.ptr == 0 || srvGpu.ptr == 0 || capacity == 0) {
            return false;
        }

        buffer_.Reset();
        mapped_ = nullptr;
        capacity_ = 0;
        cursor_ = 0;
        stats_ = {};
        stats_.srv = srvGpu;

        // 実バッファ作成に失敗しても t17 の descriptor table は常に有効にしておく。
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = BuildSurfaceGpuSceneSrvDesc(1);
        device->CreateShaderResourceView(nullptr, &nullSrvDesc, srvCpu);

        for (size_t attemptCapacity = capacity;
            attemptCapacity > 0;
            attemptCapacity = attemptCapacity > 1 ? attemptCapacity / 2 : 0) {

            const UINT64 bufferBytes =
                static_cast<UINT64>(sizeof(RUNTIME::SurfaceGpuSceneInstance)) *
                static_cast<UINT64>(attemptCapacity);

            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferBytes);
            if (FAILED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(buffer_.GetAddressOf())))) {
                buffer_.Reset();
                continue;
            }

            if (FAILED(buffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped_)))) {
                mapped_ = nullptr;
                buffer_.Reset();
                continue;
            }

            capacity_ = attemptCapacity;
            break;
        }

        if (mapped_ == nullptr || capacity_ == 0) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = BuildSurfaceGpuSceneSrvDesc(capacity_);
        device->CreateShaderResourceView(buffer_.Get(), &srvDesc, srvCpu);

        stats_.capacity = capacity_;
        stats_.initialized = true;
        return true;
    }

    void SurfaceGpuSceneFrameBuffer::ResetFrame() {
        cursor_ = 0;

        const size_t capacity = stats_.capacity;
        const bool initialized = stats_.initialized;
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = stats_.srv;
        stats_ = {};
        stats_.capacity = capacity;
        stats_.initialized = initialized;
        stats_.srv = srv;
    }

    void SurfaceGpuSceneFrameBuffer::Upload(
        const RUNTIME::SurfaceGpuSceneInstance* instances,
        size_t count) {

        stats_.requestedInstanceCount += count;
        ++stats_.uploadCallCount;
        if (instances == nullptr || count == 0) {
            return;
        }
        if (mapped_ == nullptr || capacity_ == 0) {
            stats_.overflowInstanceCount += count;
            return;
        }

        const size_t remainingCapacity = capacity_ > cursor_ ? capacity_ - cursor_ : 0;
        const size_t uploadCount = (std::min)(count, remainingCapacity);
        if (uploadCount > 0) {
            std::memcpy(
                mapped_ + cursor_,
                instances,
                sizeof(RUNTIME::SurfaceGpuSceneInstance) * uploadCount);
            cursor_ += uploadCount;
            stats_.uploadedInstanceCount += uploadCount;
        }
        if (uploadCount < count) {
            stats_.overflowInstanceCount += count - uploadCount;
        }
    }

    void SurfaceGpuSceneFrameBuffer::Upload(
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances) {

        Upload(instances.data(), instances.size());
    }

    bool SurfaceGpuSceneFrameBuffer::PatchMaterialDataIndex(
        size_t instanceIndex,
        uint32_t materialDataIndex) {

        if (mapped_ == nullptr ||
            instanceIndex >= stats_.uploadedInstanceCount ||
            instanceIndex >= capacity_) {
            return false;
        }

        mapped_[instanceIndex].materialDataIndex = materialDataIndex;
        return true;
    }

    bool SurfaceGpuSceneFrameBuffer::HasMaterialDataIndex(size_t instanceIndex) const {
        if (mapped_ == nullptr ||
            instanceIndex >= stats_.uploadedInstanceCount ||
            instanceIndex >= capacity_) {
            return false;
        }

        return mapped_[instanceIndex].materialDataIndex != RUNTIME::kInvalidRenderSurfaceIndex;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE SurfaceGpuSceneFrameBuffer::GetSrv() const {
        return stats_.srv;
    }

    const SurfaceGpuSceneFrameBufferStats& SurfaceGpuSceneFrameBuffer::GetStats() const {
        return stats_;
    }

} // namespace HIKARI::RENDER3D::CORE
