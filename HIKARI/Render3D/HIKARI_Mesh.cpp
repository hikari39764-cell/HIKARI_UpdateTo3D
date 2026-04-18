#include "Render3D/HIKARI_Mesh.h"
#include <cstring>
#include <d3dx12.h>

namespace HIKARI {

    bool Mesh::CreateStatic(ID3D12Device* device, const std::vector<VertexStatic3D>& vertices, const std::vector<uint32_t>& indices) {
        if (!device || vertices.empty() || indices.empty()) {
            return false;
        }

        const UINT vertexBufferBytes = static_cast<UINT>(sizeof(VertexStatic3D) * vertices.size());
        const UINT indexBufferBytes = static_cast<UINT>(sizeof(uint32_t) * indices.size());

        auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferBytes);
        auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

        if (FAILED(device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(vertexBuffer_.GetAddressOf())))) {
            return false;
        }

        void* vbMapped = nullptr;
        if (FAILED(vertexBuffer_->Map(0, nullptr, &vbMapped))) {
            return false;
        }
        std::memcpy(vbMapped, vertices.data(), vertexBufferBytes);
        vertexBuffer_->Unmap(0, nullptr);

        auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferBytes);
        if (FAILED(device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(indexBuffer_.GetAddressOf())))) {
            return false;
        }

        void* ibMapped = nullptr;
        if (FAILED(indexBuffer_->Map(0, nullptr, &ibMapped))) {
            return false;
        }
        std::memcpy(ibMapped, indices.data(), indexBufferBytes);
        indexBuffer_->Unmap(0, nullptr);

        vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
        vbView_.StrideInBytes = sizeof(VertexStatic3D);
        vbView_.SizeInBytes = vertexBufferBytes;

        ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
        ibView_.Format = DXGI_FORMAT_R32_UINT;
        ibView_.SizeInBytes = indexBufferBytes;

        indexCount_ = static_cast<uint32_t>(indices.size());
        return true;
    }

    bool Mesh::IsValid() const {
        return vertexBuffer_ && indexBuffer_ && indexCount_ > 0;
    }

    const D3D12_VERTEX_BUFFER_VIEW& Mesh::GetVBView() const {
        return vbView_;
    }

    const D3D12_INDEX_BUFFER_VIEW& Mesh::GetIBView() const {
        return ibView_;
    }

    uint32_t Mesh::GetIndexCount() const {
        return indexCount_;
    }

} // namespace HIKARI
