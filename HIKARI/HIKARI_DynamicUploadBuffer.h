#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

namespace HIKARI {
    namespace DX {

        class DynamicUploadBuffer {
        public:
            DynamicUploadBuffer();
            ~DynamicUploadBuffer();

            // 初始化 upload heap, size 需要 256 对齐
            void Init(ID3D12Device* device, size_t bufferSize);

            // 每次申请一段空间，自动保证 256 对齐
            void* Allocate(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress);

            void Finalize();

            // 每帧开始时调用
            void Reset();

        private:
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
            uint8_t* cpuBase_ = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpuBase_ = 0;
            size_t bufferSize_ = 0;
            size_t offset_ = 0;
        };

    } // namespace DX
} // namespace HIKARI
