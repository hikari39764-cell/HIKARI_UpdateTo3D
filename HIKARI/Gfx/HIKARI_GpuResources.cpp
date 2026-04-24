#include "HIKARI_GpuResources.h"

#include <unordered_map>
#include <cstring>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "HIKARI_Services.h"
#include "Render2D/HIKARI_DxTexture.h"

namespace HIKARI::GpuResources {

    namespace {
        constexpr uint32_t kDescriptorsPerMaterial = 4;
        constexpr uint32_t kMaxMaterialDescriptors = 4096;

        struct StaticMeshGpu {
            Microsoft::WRL::ComPtr<ID3D12Resource> vb;
            Microsoft::WRL::ComPtr<ID3D12Resource> ib;
            StaticMeshViews views{};
        };

        std::unordered_map<uint32_t, StaticMeshGpu> gStaticMeshes;
        uint32_t gNextMeshId = 1;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> gMaterialSrvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE gMaterialCpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE gMaterialGpuStart{};
        uint32_t gMaterialDescriptorSize = 0;
        uint32_t gMaterialDescriptorCursor = 0;

        bool EnsureMaterialHeap() {
            if (gMaterialSrvHeap) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (!device) {
                return false;
            }

            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.NumDescriptors = kMaxMaterialDescriptors;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(gMaterialSrvHeap.GetAddressOf())))) {
                return false;
            }

            gMaterialCpuStart = gMaterialSrvHeap->GetCPUDescriptorHandleForHeapStart();
            gMaterialGpuStart = gMaterialSrvHeap->GetGPUDescriptorHandleForHeapStart();
            gMaterialDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            gMaterialDescriptorCursor = 0;
            return true;
        }
    }

    uint32_t LoadTexture(const std::string& name, const std::string& sourcePath) {
        const int handle = DXTEX::DxTextureManager::LoadTexture(name, sourcePath);
        return handle < 0 ? 0u : static_cast<uint32_t>(handle + 1);
    }

    void GetTextureSize(uint32_t gpuTextureId, uint32_t& outWidth, uint32_t& outHeight) {
        outWidth = 0;
        outHeight = 0;
        if (gpuTextureId == 0) {
            return;
        }
        UINT w = 0;
        UINT h = 0;
        DXTEX::DxTextureManager::GetTextureSize(static_cast<int>(gpuTextureId - 1), w, h);
        outWidth = static_cast<uint32_t>(w);
        outHeight = static_cast<uint32_t>(h);
    }

    uint32_t UploadStaticMesh(const std::string&, const std::vector<ASSET::MeshAsset::Vertex>& vertices, const std::vector<uint32_t>& indices) {
        auto* device = SERVICES::gCtx.device;
        if (!device || vertices.empty() || indices.empty()) {
            return 0;
        }

        const UINT vbBytes = static_cast<UINT>(vertices.size() * sizeof(ASSET::MeshAsset::Vertex));
        const UINT ibBytes = static_cast<UINT>(indices.size() * sizeof(uint32_t));
        auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

        StaticMeshGpu gpu{};
        auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbBytes);
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(gpu.vb.GetAddressOf())))) {
            return 0;
        }
        void* vbMapped = nullptr;
        if (FAILED(gpu.vb->Map(0, nullptr, &vbMapped))) {
            return 0;
        }
        std::memcpy(vbMapped, vertices.data(), vbBytes);
        gpu.vb->Unmap(0, nullptr);

        auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibBytes);
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(gpu.ib.GetAddressOf())))) {
            return 0;
        }
        void* ibMapped = nullptr;
        if (FAILED(gpu.ib->Map(0, nullptr, &ibMapped))) {
            return 0;
        }
        std::memcpy(ibMapped, indices.data(), ibBytes);
        gpu.ib->Unmap(0, nullptr);

        gpu.views.vbv.BufferLocation = gpu.vb->GetGPUVirtualAddress();
        gpu.views.vbv.SizeInBytes = vbBytes;
        gpu.views.vbv.StrideInBytes = sizeof(ASSET::MeshAsset::Vertex);
        gpu.views.ibv.BufferLocation = gpu.ib->GetGPUVirtualAddress();
        gpu.views.ibv.SizeInBytes = ibBytes;
        gpu.views.ibv.Format = DXGI_FORMAT_R32_UINT;
        gpu.views.indexCount = static_cast<uint32_t>(indices.size());
        gpu.views.valid = true;

        const uint32_t id = gNextMeshId++;
        gStaticMeshes.emplace(id, std::move(gpu));
        return id;
    }

    StaticMeshViews GetStaticMeshViews(uint32_t gpuMeshId) {
        if (gpuMeshId == 0) {
            return {};
        }
        const auto it = gStaticMeshes.find(gpuMeshId);
        return (it == gStaticMeshes.end()) ? StaticMeshViews{} : it->second.views;
    }

    ID3D12DescriptorHeap* GetSrvHeap() {
        return DXTEX::DxTextureManager::GetSrvHeap();
    }

    ID3D12DescriptorHeap* GetMaterialSrvHeap() {
        return EnsureMaterialHeap() ? gMaterialSrvHeap.Get() : nullptr;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t gpuTextureId) {
        if (gpuTextureId == 0) {
            return {};
        }
        return DXTEX::DxTextureManager::GetSrvGpuHandle(static_cast<int>(gpuTextureId - 1));
    }

    MaterialSrvBlock AllocateMaterialSrvBlock(uint32_t baseColorTex, uint32_t normalTex, uint32_t ormTex, uint32_t emissiveTex) {
        MaterialSrvBlock block{};
        if (!EnsureMaterialHeap()) {
            return block;
        }

        if (gMaterialDescriptorCursor + kDescriptorsPerMaterial > kMaxMaterialDescriptors) {
            gMaterialDescriptorCursor = 0;
        }

        auto* device = SERVICES::gCtx.device;
        if (!device) {
            return block;
        }

        const uint32_t textureIds[kDescriptorsPerMaterial] = { baseColorTex, normalTex, ormTex, emissiveTex };
        for (uint32_t i = 0; i < kDescriptorsPerMaterial; ++i) {
            const D3D12_CPU_DESCRIPTOR_HANDLE src = DXTEX::DxTextureManager::GetSrvCpuHandle(static_cast<int>(textureIds[i] > 0 ? textureIds[i] - 1 : -1));
            if (src.ptr == 0) {
                return {};
            }

            D3D12_CPU_DESCRIPTOR_HANDLE dst = gMaterialCpuStart;
            dst.ptr += static_cast<SIZE_T>(gMaterialDescriptorCursor + i) * gMaterialDescriptorSize;
            device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        block.gpuStart = gMaterialGpuStart;
        block.gpuStart.ptr += static_cast<UINT64>(gMaterialDescriptorCursor) * gMaterialDescriptorSize;
        block.valid = true;
        gMaterialDescriptorCursor += kDescriptorsPerMaterial;
        return block;
    }

    void ResetMaterialSrvAllocator() {
        gMaterialDescriptorCursor = 0;
    }

} // namespace HIKARI::GpuResources
