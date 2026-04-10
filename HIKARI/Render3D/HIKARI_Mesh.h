#pragma once
#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>
#include "HIKARI_Math3D.h"

namespace HIKARI {

    struct VertexStatic3D {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        float u = 0.0f;
        float v = 0.0f;
    };

    class Mesh {
    public:
        bool CreateStatic(ID3D12Device* device, const std::vector<VertexStatic3D>& vertices, const std::vector<uint32_t>& indices);

        bool IsValid() const;
        const D3D12_VERTEX_BUFFER_VIEW& GetVBView() const;
        const D3D12_INDEX_BUFFER_VIEW& GetIBView() const;
        uint32_t GetIndexCount() const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vbView_{};
        D3D12_INDEX_BUFFER_VIEW ibView_{};
        uint32_t indexCount_ = 0;
    };

} // namespace HIKARI
