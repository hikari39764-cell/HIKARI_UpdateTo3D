#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace HIKARI {
    namespace DXTEX {

        class DxTextureManager {
        public:
            static void Init(int maxTextures = 128);
            static void Finalize();

            // name: 逻辑名（比如 "Player"）
            // path: 相对 exe 的路径（比如 "./deformTest.png"）
            // 返回: 内部 handle（失败时 -1）
            static int LoadTexture(const std::string& name, const std::string& path);

            static int RegisterFromResource(ID3D12Resource* resource);

            static D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(int handle);


            static ID3D12DescriptorHeap* GetSrvHeap();

            static void GetTextureSize(int handle, UINT& outWidth, UINT& outHeight);



        private:
            static void EnsureInit();
            static int  CreateTextureFromFile(const std::string& path);

            DxTextureManager() = default;
            ~DxTextureManager() = default;

            DxTextureManager(const DxTextureManager&) = delete;
            DxTextureManager& operator=(const DxTextureManager&) = delete;

        private:
            static bool initialized_;
            static UINT descriptorSize_;

            static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;

            static std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_;

            static std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvCpu_;
            static std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> srvGpu_;

            static std::unordered_map<std::string, int> nameToHandle_;

            static int nextIndex_;
        };

    } // namespace DXTEX
} // namespace HIKARI
