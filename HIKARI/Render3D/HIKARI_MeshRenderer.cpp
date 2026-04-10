#include "HIKARI_MeshRenderer.h"
#include <array>
#include <cstring>
#include <vector>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>
#include "HIKARI_Services.h"
#include "HIKARI_D3DBlobCompat.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::MESHRENDERER {

    using Microsoft::WRL::ComPtr;

    namespace {
        struct CameraCB {
            MATH::Mat4 viewProj{};
            MATH::Vec4 cameraPos{};
        };

        struct ObjectCB {
            MATH::Mat4 world{};
            MATH::Vec4 baseColor{};
        };

        struct DrawItem {
            const ModelAsset* asset = nullptr;
            Transform3D transform{};
        };

        struct State {
            bool initialized = false;
            ComPtr<ID3D12RootSignature> rootSig;
            ComPtr<ID3D12PipelineState> pso;
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> objectCB;
            CameraCB* cameraMapped = nullptr;
            ObjectCB* objectMapped = nullptr;
            std::vector<DrawItem> drawItems;
        };

        State g;

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cameraBytes = (sizeof(CameraCB) + 255u) & ~255u;
            const UINT objectBytes = (sizeof(ObjectCB) * 2048u + 255u) & ~255u;

            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto cameraDesc = CD3DX12_RESOURCE_DESC::Buffer(cameraBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &cameraDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.cameraCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.cameraCB->Map(0, nullptr, reinterpret_cast<void**>(&g.cameraMapped)))) {
                return false;
            }

            auto objectDesc = CD3DX12_RESOURCE_DESC::Buffer(objectBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &objectDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.objectCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.objectCB->Map(0, nullptr, reinterpret_cast<void**>(&g.objectMapped)))) {
                return false;
            }
            return true;
        }

        bool CreatePipeline(ID3D12Device* device) {
            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            ComPtr<ID3DBlob> err;
            if (FAILED(D3DCompileFromFile(L"NoviceResources/shaders/Render3D_StaticVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, g.vsBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"NoviceResources/shaders/Render3D_StaticPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, g.psBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;
            params[0].Descriptor.RegisterSpace = 0;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[1].Descriptor.ShaderRegister = 1;
            params[1].Descriptor.RegisterSpace = 0;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                return false;
            }

            if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.rootSig.GetAddressOf())))) {
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(VertexStatic3D, u)),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { g.vsBlob->GetBufferPointer(), g.vsBlob->GetBufferSize() };
            psoDesc.PS = { g.psBlob->GetBufferPointer(), g.psBlob->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.pso.GetAddressOf())));
        }

        bool EnsureInitialized() {
            if (g.initialized) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (!device) {
                return false;
            }

            if (!CreateBuffers(device)) {
                return false;
            }
            if (!CreatePipeline(device)) {
                return false;
            }

            g.initialized = true;
            return true;
        }
    }

    void Reset() {
        g.drawItems.clear();
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform) {
        g.drawItems.push_back({ &asset, transform });
    }

    void RenderAll(const Camera3D& camera) {
        if (g.drawItems.empty()) {
            return;
        }
        if (!EnsureInitialized()) {
            return;
        }

        auto* cmd = SERVICES::gCtx.cmdList;
        if (!cmd) {
            return;
        }

        g.cameraMapped->viewProj = camera.GetViewProj();
        const MATH::Vec3 cameraPos = camera.GetPosition();
        g.cameraMapped->cameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 1.0f };

        cmd->SetGraphicsRootSignature(g.rootSig.Get());
        cmd->SetPipelineState(g.pso.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // MeshRenderer runs inside the active scene render pass.
        // Do not switch render targets here (especially not back to SERVICES::gCtx.rtv),
        // otherwise scene-capture/present routing managed by the frame/post pipeline breaks.
        // If an offscreen mesh preview is needed in the future, pass explicit render-target/context in.

        cmd->SetGraphicsRootConstantBufferView(0, g.cameraCB->GetGPUVirtualAddress());

        constexpr UINT kObjectStride = (sizeof(ObjectCB) + 255u) & ~255u;

        for (size_t i = 0; i < g.drawItems.size(); ++i) {
            const DrawItem& item = g.drawItems[i];
            if (!item.asset || !item.asset->GetMesh() || !item.asset->GetMesh()->IsValid()) {
                continue;
            }

            ObjectCB obj{};
            obj.world = item.transform.GetWorldMatrix();
            if (const Material* material = item.asset->GetMaterial()) {
                obj.baseColor = material->GetBaseColor();
            } else {
                obj.baseColor = { 1,1,1,1 };
            }

            uint8_t* dst = reinterpret_cast<uint8_t*>(g.objectMapped) + static_cast<size_t>(kObjectStride) * i;
            std::memcpy(dst, &obj, sizeof(ObjectCB));

            const D3D12_GPU_VIRTUAL_ADDRESS objAddress = g.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(kObjectStride) * i;
            cmd->SetGraphicsRootConstantBufferView(1, objAddress);

            const Mesh* mesh = item.asset->GetMesh();
            D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
            cmd->IASetVertexBuffers(0, 1, &vb);
            cmd->IASetIndexBuffer(&ib);
            cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
        }
    }

} // namespace HIKARI::MESHRENDERER
