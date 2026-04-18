#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "HIKARI_D3DBlobCompat.h"
#include <Windows.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <cassert>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace POST {

        namespace {
            static void OutputError(ID3DBlob* err)
            {
                if (!err) { return; }
                OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
            }

            const char* kFullscreenVS = R"(
struct VS_OUT {
  float4 pos : SV_POSITION;
  float2 uv  : TEXCOORD0;
};
VS_OUT main(uint vid : SV_VertexID)
{
  VS_OUT o;
  float2 pos;
  if (vid == 0) pos = float2(-1.0, -1.0);
  else if (vid == 1) pos = float2(-1.0,  3.0);
  else pos = float2( 3.0, -1.0);
  o.pos = float4(pos, 0, 1);
  o.uv  = float2((pos.x + 1) * 0.5, 1 - (pos.y + 1) * 0.5);
  return o;
}
)";

            const char* kCopyPS = R"(
Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);
struct PS_IN {
  float4 pos : SV_POSITION;
  float2 uv  : TEXCOORD0;
};
float4 main(PS_IN i) : SV_TARGET
{
  return gTex.Sample(gSamp, i.uv);
}
)";
        }

        bool QuadDrawer::Init(const GFX::Context& ctx)
        {
            context_ = ctx;
            if (initialized_) { return true; }

            // 编译 Shader
            {
                ComPtr<ID3DBlob> err;
                HRESULT hr = D3DCompile(kFullscreenVS, std::strlen(kFullscreenVS), nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, vsBlob_.GetAddressOf(), err.GetAddressOf());
                if (FAILED(hr)) { OutputError(err.Get()); return false; }
            }
            {
                ComPtr<ID3DBlob> err;
                HRESULT hr = D3DCompile(kCopyPS, std::strlen(kCopyPS), nullptr, nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, psCopyBlob_.GetAddressOf(), err.GetAddressOf());
                if (FAILED(hr)) { OutputError(err.Get()); return false; }
            }

            if (!CreateRootSignature()) { return false; }
            if (!CreateBlendPipelines()) { return false; }
            if (!CreatePipeline(psCopyBlob_.Get(), psoCopy_)) { return false; }

            currentPostPS_ = psCopyBlob_.Get();
            if (!CreatePipeline(currentPostPS_, psoPost_)) { return false; }

            initialized_ = true;
            return true;
        }

        bool QuadDrawer::CreateBlendPipelines()
        {
            auto* device = context_.device;
            if (!device) { OutputDebugStringA("[PostQuadDrawer] device is null in CreateBlendPipelines\n"); return false; }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
            pso.pRootSignature = rootSig_.Get();
            pso.VS = { vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize() };
            pso.PS = { psCopyBlob_->GetBufferPointer(), psCopyBlob_->GetBufferSize() };
            pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            pso.DepthStencilState.DepthEnable = FALSE;
            pso.DepthStencilState.StencilEnable = FALSE;
            pso.SampleMask = UINT_MAX;
            pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets = 1;
            pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc.Count = 1;

            D3D12_RENDER_TARGET_BLEND_DESC blendDesc{};
            blendDesc.BlendEnable = TRUE;
            blendDesc.LogicOpEnable = FALSE;
            blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            // 1. Alpha Blend
            blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
            blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
            blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            pso.BlendState.RenderTarget[0] = blendDesc;
            if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(psoBlendAlpha_.GetAddressOf())))) return false;

            // 2. Additive Blend
            blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.DestBlend = D3D12_BLEND_ONE;
            pso.BlendState.RenderTarget[0] = blendDesc;
            if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(psoBlendAdd_.GetAddressOf())))) return false;

            // 3. Multiply Blend (新增)
            // 公式: Final = (SrcColor * DestColor) + (DestColor * 0)
            // 效果: 背景色(Dest) 乘以 光照贴图色(Src)
            blendDesc.SrcBlend = D3D12_BLEND_DEST_COLOR;
            blendDesc.DestBlend = D3D12_BLEND_ZERO;
            // Alpha处理保持默认即可，或者 One/Zero
            blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE; 
            blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
            pso.BlendState.RenderTarget[0] = blendDesc;
            if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(psoBlendMultiply_.GetAddressOf())))) return false;

            return true;
        }

        void QuadDrawer::UpdateContext(const GFX::Context& ctx)
        {
            context_ = ctx;
        }

        void QuadDrawer::Finalize()
        {
            psoPost_.Reset();
            psoCopy_.Reset();
            psoBlendAlpha_.Reset();
            psoBlendAdd_.Reset();
            psoBlendMultiply_.Reset(); // [新增]
            rootSig_.Reset();
            vsBlob_.Reset();
            psCopyBlob_.Reset();
            currentPostPS_ = nullptr;
            currentSrvHeap_ = nullptr;
            currentCBV0_ = 0;
            initialized_ = false;
        }

        // ... CreateRootSignature, CreatePipeline 保持不变 ...
        bool QuadDrawer::CreateRootSignature()
        {
            auto* device = context_.device;
            if (!device) { OutputDebugStringA("[PostQuadDrawer] device is null in CreatePipeline\n"); return false; }
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = 0;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            D3D12_ROOT_PARAMETER rp[2]{};
            rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            rp[0].DescriptorTable.NumDescriptorRanges = 1;
            rp[0].DescriptorTable.pDescriptorRanges = &range;
            rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            rp[1].Descriptor.ShaderRegister = 0;
            rp[1].Descriptor.RegisterSpace = 0;
            D3D12_STATIC_SAMPLER_DESC samp{};
            samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            samp.MinLOD = 0;
            samp.MaxLOD = D3D12_FLOAT32_MAX;
            samp.ShaderRegister = 0;
            samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 2;
            rsDesc.pParameters = rp;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers = &samp;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ComPtr<ID3DBlob> sig, err;
            HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sig.GetAddressOf(), err.GetAddressOf());
            if (FAILED(hr)) { OutputError(err.Get()); return false; }
            hr = device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(rootSig_.GetAddressOf()));
            return SUCCEEDED(hr);
        }

        bool QuadDrawer::CreatePipeline(ID3DBlob* psBlob, ComPtr<ID3D12PipelineState>& outPso)
        {
            auto* device = context_.device;
            if (!device) { OutputDebugStringA("[PostQuadDrawer] device is null in CreatePipeline\n"); return false; }
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
            pso.pRootSignature = rootSig_.Get();
            pso.VS = { vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize() };
            pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            pso.DepthStencilState.DepthEnable = FALSE;
            pso.DepthStencilState.StencilEnable = FALSE;
            pso.SampleMask = UINT_MAX;
            pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets = 1;
            pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(outPso.GetAddressOf())));
        }

        void QuadDrawer::DrawBlended(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu, BlendOption mode)
        {
            SetInputTexture(srvHeap, srvGpu);
            auto* cmd = context_.cmdList;
            if (!cmd) { return; }
            if (!currentSrvHeap_ || currentSrvGpu_.ptr == 0) {
                OutputDebugStringA("[PostQuadDrawer] DrawBlended skipped: invalid SRV heap/handle.\n");
                return;
            }
            if (!rootSig_) {
                OutputDebugStringA("[PostQuadDrawer] DrawBlended skipped: root signature is null.\n");
                return;
            }
            ID3D12DescriptorHeap* heaps[] = { currentSrvHeap_ };
            cmd->SetDescriptorHeaps(1, heaps);
            cmd->SetGraphicsRootSignature(rootSig_.Get());

            ID3D12PipelineState* selectedPso = nullptr;
            if (mode == BlendOption::Additive) {
                selectedPso = psoBlendAdd_.Get();
            }
            else if (mode == BlendOption::Multiply) {
                selectedPso = psoBlendMultiply_.Get();
            }
            else {
                selectedPso = psoBlendAlpha_.Get();
            }
            if ((mode == BlendOption::Additive && !psoBlendAdd_) ||
                (mode == BlendOption::Multiply && !psoBlendMultiply_) ||
                (mode == BlendOption::Alpha && !psoBlendAlpha_)) {
                OutputDebugStringA("[PostQuadDrawer] DrawBlended skipped: blend PSO is null.\n");
                return;
            }
            cmd->SetPipelineState(selectedPso);

            cmd->SetGraphicsRootDescriptorTable(0, currentSrvGpu_);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->DrawInstanced(3, 1, 0, 0);
        }

        void QuadDrawer::SetInputTexture(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu)
        {
            currentSrvHeap_ = srvHeap;
            currentSrvGpu_ = srvGpu;
            if (!currentSrvHeap_ || currentSrvGpu_.ptr == 0) {
                OutputDebugStringA("[PostQuadDrawer] SetInputTexture received invalid SRV heap/handle.\n");
            }
        }

        void QuadDrawer::SetPixelShader(ID3DBlob* psBlob)
        {
            if (!psBlob) { return; }
            if (psBlob == currentPostPS_ && psoPost_) { return; }
            currentPostPS_ = psBlob;
            CreatePipeline(psBlob, psoPost_);
        }

        void QuadDrawer::SetConstantBuffer(D3D12_GPU_VIRTUAL_ADDRESS cbv0)
        {
            currentCBV0_ = cbv0;
        }

        void QuadDrawer::DrawFullscreen(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu)
        {
            SetInputTexture(srvHeap, srvGpu);
            auto* cmd = context_.cmdList;
            if (!cmd) { return; }
            if (!currentSrvHeap_ || currentSrvGpu_.ptr == 0) {
                OutputDebugStringA("[PostQuadDrawer] DrawFullscreen(copy) skipped: invalid SRV heap/handle.\n");
                return;
            }
            if (!rootSig_ || !psoCopy_) {
                OutputDebugStringA("[PostQuadDrawer] DrawFullscreen(copy) skipped: root signature/PSO is null.\n");
                return;
            }
            ID3D12DescriptorHeap* heaps[] = { currentSrvHeap_ };
            cmd->SetDescriptorHeaps(1, heaps);
            cmd->SetGraphicsRootSignature(rootSig_.Get());
            cmd->SetPipelineState(psoCopy_.Get());
            cmd->SetGraphicsRootDescriptorTable(0, currentSrvGpu_);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->DrawInstanced(3, 1, 0, 0);
        }

        void QuadDrawer::DrawFullscreen()
        {
            auto* cmd = context_.cmdList;
            if (!cmd) { return; }
            if (!currentSrvHeap_ || currentSrvGpu_.ptr == 0) {
                OutputDebugStringA("[PostQuadDrawer] DrawFullscreen(post) skipped: invalid SRV heap/handle.\n");
                return;
            }
            if (!rootSig_ || !psoPost_) {
                OutputDebugStringA("[PostQuadDrawer] DrawFullscreen(post) skipped: root signature/PSO is null.\n");
                return;
            }
            ID3D12DescriptorHeap* heaps[] = { currentSrvHeap_ };
            cmd->SetDescriptorHeaps(1, heaps);
            cmd->SetGraphicsRootSignature(rootSig_.Get());
            cmd->SetPipelineState(psoPost_.Get());
            cmd->SetGraphicsRootDescriptorTable(0, currentSrvGpu_);
            if (currentCBV0_ != 0) {
                cmd->SetGraphicsRootConstantBufferView(1, currentCBV0_);
            }
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->DrawInstanced(3, 1, 0, 0);
        }

    } // POST
} // HIKARI
