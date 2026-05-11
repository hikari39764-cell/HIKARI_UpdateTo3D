#include "HIKARI_SkyRenderer.h"

#include <algorithm>
#include <cmath>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "HIKARI_DxTexture.h"
#include "Render3D/HIKARI_Material.h"
#include "HIKARI_Services.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::SKYRENDERER {

    using Microsoft::WRL::ComPtr;

    namespace {
        struct SkyCB {
            MATH::Mat4 worldViewProj{};
            MATH::Vec4 tintExposure{};
        };

        struct State {
            bool initialized = false;
            ComPtr<ID3D12RootSignature> rootSig;
            ComPtr<ID3D12PipelineState> pso;
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            ComPtr<ID3D12Resource> cb;
            SkyCB* mapped = nullptr;
            std::string loadedTexturePath{};
            int textureHandle = -1;
            int fallbackTextureHandle = -1;
            SkyRendererDebugState debug{};
        };

        State g;

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cbBytes = (sizeof(SkyCB) + 255u) & ~255u;
            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(cbBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.cb.GetAddressOf())))) {
                return false;
            }
            return SUCCEEDED(g.cb->Map(0, nullptr, reinterpret_cast<void**>(&g.mapped)));
        }

        bool CreatePipeline(ID3D12Device* device) {
            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            ComPtr<ID3DBlob> err;
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_SkyVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, g.vsBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_SkyPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, g.psBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }

            D3D12_DESCRIPTOR_RANGE textureRange{};
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = 1;
            textureRange.BaseShaderRegister = 0;
            textureRange.RegisterSpace = 0;
            textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &textureRange;

            D3D12_STATIC_SAMPLER_DESC linearWrapSampler{};
            linearWrapSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            linearWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            linearWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            linearWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            linearWrapSampler.ShaderRegister = 0;
            linearWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            linearWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers = &linearWrapSampler;
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
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
            if (!CreateBuffers(device) || !CreatePipeline(device)) {
                return false;
            }
            g.fallbackTextureHandle = DXTEX::DxTextureManager::LoadTexture("sky_renderer/fallback_white", "HIKARI/white1x1.png");
            g.initialized = true;
            g.debug.initialized = true;
            return true;
        }
    }

    void Reset() {
        g.debug.lastRenderSubmitted = false;
        g.debug.skyAssetFound = false;
        g.debug.skyMeshLoaded = false;
        g.debug.skyMeshValid = false;
        g.debug.textureValid = false;
    }

    void Render(const Camera3D& camera, const SkySettings& settings, ModelManager& modelManager, SkyManager& skyManager) {
        g.debug.initialized = g.initialized;
        g.debug.activeSkyAsset = settings.skyAsset;
        g.debug.activeTexturePath.clear();

        if (!settings.enabled || !EnsureInitialized()) {
            g.debug.initialized = g.initialized;
            return;
        }

        const SkyAsset* skyAsset = skyManager.FindAsset(settings.skyAsset);
        g.debug.skyAssetFound = (skyAsset != nullptr);
        if (!skyAsset) {
            return;
        }

        g.debug.activeTexturePath = skyAsset->texturePath;

        const ModelAsset* modelAsset = modelManager.FindAsset(skyAsset->meshAssetName);
        g.debug.skyMeshLoaded = (modelAsset != nullptr && modelAsset->GetState() == ModelAsset::State::Loaded);
        g.debug.skyMeshValid = g.debug.skyMeshLoaded && modelAsset->GetMesh() && modelAsset->GetMesh()->IsValid();
        if (!g.debug.skyMeshValid) {
            return;
        }

        if (g.loadedTexturePath != skyAsset->texturePath) {
            g.loadedTexturePath = skyAsset->texturePath;
            g.textureHandle = -1;
            if (!skyAsset->texturePath.empty()) {
                g.textureHandle = DXTEX::DxTextureManager::LoadTexture("sky_renderer/scene_sky", skyAsset->texturePath.c_str());
            }
        }

        auto* cmd = SERVICES::gCtx.cmdList;
        if (!cmd) {
            return;
        }

        const MATH::Vec3 cameraPos = settings.followCamera ? camera.GetPosition() : MATH::Vec3{};
        const MATH::Quat yawRot = MATH::Quat::FromEulerXYZ(0.0f, settings.yaw, 0.0f);
        const float s = std::max(0.0001f, settings.scale);
        const MATH::Mat4 world = MATH::Mat4::TRS(cameraPos, yawRot, { s, s, s });
        g.mapped->worldViewProj = camera.GetViewProj() * world;
        g.mapped->tintExposure = { settings.tint.x, settings.tint.y, settings.tint.z, std::max(0.0f, settings.exposure) };

        cmd->SetGraphicsRootSignature(g.rootSig.Get());
        cmd->SetPipelineState(g.pso.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, g.cb->GetGPUVirtualAddress());

        ID3D12DescriptorHeap* srvHeap = DXTEX::DxTextureManager::GetSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        const int textureHandle = (g.textureHandle >= 0) ? g.textureHandle : g.fallbackTextureHandle;
        const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textureHandle);
        g.debug.textureValid = (textureSrv.ptr != 0);
        if (textureSrv.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(1, textureSrv);
        }

        const Mesh* mesh = modelAsset->GetMesh();
        D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
        D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
        cmd->IASetVertexBuffers(0, 1, &vb);
        cmd->IASetIndexBuffer(&ib);
        cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
        g.debug.lastRenderSubmitted = true;
    }

    const SkyRendererDebugState& GetDebugState() {
        return g.debug;
    }

} // namespace HIKARI::SKYRENDERER
