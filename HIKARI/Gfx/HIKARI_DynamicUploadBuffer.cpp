#include "HIKARI_DynamicUploadBuffer.h"
#include <cassert>
#include <d3dx12.h>

using namespace HIKARI::DX;

static inline size_t Align256(size_t size) {
    return (size + 255) & ~255;
}

DynamicUploadBuffer::DynamicUploadBuffer() {}
DynamicUploadBuffer::~DynamicUploadBuffer() {}

void DynamicUploadBuffer::Init(ID3D12Device* device, size_t bufferSize)
{
    bufferSize_ = Align256(bufferSize);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize_);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&buffer_)
    );
    assert(SUCCEEDED(hr));

    buffer_->Map(0, nullptr, reinterpret_cast<void**>(&cpuBase_));
    gpuBase_ = buffer_->GetGPUVirtualAddress();

    offset_ = 0;
}

void* DynamicUploadBuffer::Allocate(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress)
{
    size_t aligned = Align256(size);

    if (offset_ + aligned > bufferSize_) {
        offset_ = 0; // ｻﾘｻｷ
    }

    void* cpuAddr = cpuBase_ + offset_;
    gpuAddress = gpuBase_ + offset_;

    offset_ += aligned;

    return cpuAddr;
}
void DynamicUploadBuffer::Finalize()
{
    if (buffer_) {
        buffer_->Unmap(0, nullptr);
    }
    buffer_.Reset();
    cpuBase_ = nullptr;
    gpuBase_ = 0;
    offset_ = 0;
    bufferSize_ = 0;
}

void DynamicUploadBuffer::Reset()
{
    offset_ = 0;
}
