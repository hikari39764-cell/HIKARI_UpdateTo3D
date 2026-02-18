#include "HIKARI_Offscreen.h"
#include "HIKARI_DxTexture.h"
#include <d3dcompiler.h>
#include <cassert>
#pragma comment(lib, "d3dcompiler.lib")

using namespace KamataEngine;
using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace OFFSCREEN {

        namespace {

            /// メモリ上の HLSL 文字列（フルスクリーントライアングル）
            const char* kFullscreenVS = R"(
struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VS_OUTPUT main(uint vid : SV_VertexID)
{
    VS_OUTPUT o;

    // フルスクリーントライアングル
    float2 pos;
    if (vid == 0) {
        pos = float2(-1.0f, -1.0f);
    } else if (vid == 1) {
        pos = float2(-1.0f,  3.0f);
    } else {
        pos = float2( 3.0f, -1.0f);
    }

    o.pos = float4(pos, 0.0f, 1.0f);

    // [-1,1] -> [0,1] へ変換（UV は上下反転）
    o.uv = float2( (pos.x + 1.0f) * 0.5f, 1.0f - (pos.y + 1.0f) * 0.5f );
    return o;
}
)";

            const char* kFullscreenPS = R"(
Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    // いまはただサンプルするだけ（ここにポストエフェクトを書ける）
    float4 col = gTex.Sample(gSamp, input.uv);
    return col;
}
)";

        } // namespace

        // ======================== Offscreen 本体 ============================

        Offscreen& Offscreen::Instance() {
            static Offscreen s;
            return s;
        }

        bool Offscreen::IsInitialized() {
            return Instance().initialized_;
        }

        int Offscreen::GetTextureHandle() {
            return Instance().textureHandle_;
        }

        void Offscreen::Init(int width, int height) {
            Offscreen& self = Instance();
            if (self.initialized_) {
                return;
            }

            self.dxCommon_ = DirectXCommon::GetInstance();
            self.CreateResources(width, height);
            self.CreatePipeline();
            self.initialized_ = true;
        }

        void Offscreen::Finalize() {
            Offscreen& self = Instance();
            self.DestroyResources();
            self.initialized_ = false;
        }

        void Offscreen::CreateResources(int width, int height) {
            dxCommon_ = DirectXCommon::GetInstance();
            ID3D12Device* device = dxCommon_->GetDevice();

            width_ = width;
            height_ = height;

            // ---------------------------------------
            // カラーターゲット用テクスチャ
            // ---------------------------------------
            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 0.0f;

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
            CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                static_cast<UINT64>(width_),
                static_cast<UINT>(height_),
                1, 1, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
            );

            HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &clearValue,
                IID_PPV_ARGS(&colorTex_)
            );
            assert(SUCCEEDED(hr));
            (void)hr;

            colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

            // ---------------------------------------
            // RTV ヒープ
            // ---------------------------------------
            D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
            rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDesc.NumDescriptors = 1;
            rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_));
            assert(SUCCEEDED(hr));

            rtvHandle_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

            device->CreateRenderTargetView(colorTex_.Get(), nullptr, rtvHandle_);

            // ---------------------------------------
            // SRV ヒープ（シェーダーから読む用）
            // ---------------------------------------
            D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
            srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvDesc.NumDescriptors = 1;
            srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            hr = device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));
            assert(SUCCEEDED(hr));

            srvCpuHandle_ = srvHeap_->GetCPUDescriptorHandleForHeapStart();
            srvGpuHandle_ = srvHeap_->GetGPUDescriptorHandleForHeapStart();

            D3D12_SHADER_RESOURCE_VIEW_DESC srvView{};
            srvView.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            srvView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvView.Texture2D.MipLevels = 1;

            device->CreateShaderResourceView(colorTex_.Get(), &srvView, srvCpuHandle_);
            textureHandle_ = HIKARI::DXTEX::DxTextureManager::RegisterFromResource(colorTex_.Get());
            // ---------------------------------------
            // ビューポート / シザー
            // ---------------------------------------
            viewport_.TopLeftX = 0.0f;
            viewport_.TopLeftY = 0.0f;
            viewport_.Width = static_cast<float>(width_);
            viewport_.Height = static_cast<float>(height_);
            viewport_.MinDepth = 0.0f;
            viewport_.MaxDepth = 1.0f;

            scissorRect_.left = 0;
            scissorRect_.top = 0;
            scissorRect_.right = width_;
            scissorRect_.bottom = height_;
        }

        void Offscreen::DestroyResources() {
            colorTex_.Reset();
            rtvHeap_.Reset();
            srvHeap_.Reset();
            rootSignature_.Reset();
            pipelineState_.Reset();
        }

        void Offscreen::CreatePipeline() {
            ID3D12Device* device = dxCommon_->GetDevice();

            // ---------------------------------------
            // シェーダコンパイル
            // ---------------------------------------
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            ComPtr<ID3DBlob> errorBlob;

            HRESULT hr = D3DCompile(
                kFullscreenVS, strlen(kFullscreenVS),
                nullptr, nullptr, nullptr,
                "main", "vs_5_0",
                0, 0,
                &vsBlob, &errorBlob
            );
            if (FAILED(hr)) {
                if (errorBlob) {
                    OutputDebugStringA((char*)errorBlob->GetBufferPointer());
                }
                assert(false);
            }

            hr = D3DCompile(
                kFullscreenPS, strlen(kFullscreenPS),
                nullptr, nullptr, nullptr,
                "main", "ps_5_0",
                0, 0,
                &psBlob, &errorBlob
            );
            if (FAILED(hr)) {
                if (errorBlob) {
                    OutputDebugStringA((char*)errorBlob->GetBufferPointer());
                }
                assert(false);
            }

            // ---------------------------------------
            // ルートシグネチャ
            // t0 だけを持つ SRV テーブル + s0 サンプラー
            // ---------------------------------------
            CD3DX12_DESCRIPTOR_RANGE range{};
            range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

            CD3DX12_ROOT_PARAMETER rootParam{};
            rootParam.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

            CD3DX12_STATIC_SAMPLER_DESC samplerDesc(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.Init_1_0(1, &rootParam, 1, &samplerDesc,
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

            ComPtr<ID3DBlob> rsBlob;
            ComPtr<ID3DBlob> rsErrorBlob;
            hr = D3DX12SerializeVersionedRootSignature(
                &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                &rsBlob, &rsErrorBlob
            );
            if (FAILED(hr)) {
                if (rsErrorBlob) {
                    OutputDebugStringA((char*)rsErrorBlob->GetBufferPointer());
                }
                assert(false);
            }

            hr = device->CreateRootSignature(
                0,
                rsBlob->GetBufferPointer(),
                rsBlob->GetBufferSize(),
                IID_PPV_ARGS(&rootSignature_)
            );
            assert(SUCCEEDED(hr));

            // ---------------------------------------
            // グラフィックスパイプライン
            // フルスクリーントライアングル（頂点レイアウトなし）
            // ---------------------------------------
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());

            psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

            auto& rt0 = psoDesc.BlendState.RenderTarget[0];

            rt0.BlendEnable = FALSE;

            // 预乘 alpha：颜色通道
            rt0.SrcBlend = D3D12_BLEND_ONE;            // src.rgb 已经乘过 alpha
            rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;  // 用 alpha 做权重

            // alpha 通道本身（其实 backbuffer 的 alpha 你不太用到，简单处理就行）
            rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

            rt0.BlendOp = D3D12_BLEND_OP_ADD;
            rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;


            rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = FALSE;

            // 頂点レイアウトなし（SV_VertexID を使う）
            psoDesc.InputLayout.pInputElementDescs = nullptr;
            psoDesc.InputLayout.NumElements = 0;

            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

            psoDesc.NumRenderTargets = 1;
            // Novice のバックバッファは sRGB なのでそれに合わせる
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            psoDesc.SampleDesc.Count = 1;

            psoDesc.pRootSignature = rootSignature_.Get();

            hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
            assert(SUCCEEDED(hr));
        }

        void Offscreen::BeginScene() {
            Offscreen& self = Instance();
            if (!self.initialized_) {
                return;
            }
            auto* dx = KamataEngine::DirectXCommon::GetInstance();
            dx->SetRenderTargets(false);
            ID3D12GraphicsCommandList* cmd = self.dxCommon_->GetCommandList();

            // オフスクリーンをレンダーターゲットにセット
            if (self.colorState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    self.colorTex_.Get(),
                    self.colorState_,
                    D3D12_RESOURCE_STATE_RENDER_TARGET
                );
                cmd->ResourceBarrier(1, &barrier);
                self.colorState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }

            cmd->OMSetRenderTargets(1, &self.rtvHandle_, FALSE, nullptr);


            // クリア
            float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            cmd->ClearRenderTargetView(self.rtvHandle_, clearColor, 0, nullptr);

            cmd->RSSetViewports(1, &self.viewport_);
            cmd->RSSetScissorRects(1, &self.scissorRect_);
        }

        void Offscreen::EndScene() {
            Offscreen& self = Instance();
            if (!self.initialized_) {
                return;
            }

            auto* dx = KamataEngine::DirectXCommon::GetInstance();
            auto* cmd = dx->GetCommandList();

            // 把 offscreen RT 切回 PixelShaderResource 状态
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = self.colorTex_.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            self.colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            dx->SetRenderTargets(true);
        }

        void Offscreen::DrawToScreen() {
            Offscreen& self = Instance();
            if (!self.initialized_) return;

            auto* dx = DirectXCommon::GetInstance();
            auto* cmd = dx->GetCommandList();

            dx->SetRenderTargets(true);

            ID3D12DescriptorHeap* heaps[] = { self.srvHeap_.Get() };
            cmd->SetDescriptorHeaps(1, heaps);

            cmd->SetGraphicsRootSignature(self.rootSignature_.Get());
            cmd->SetPipelineState(self.pipelineState_.Get());

            // Fullscreen Triangle
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->IASetVertexBuffers(0, 0, nullptr);

            cmd->SetGraphicsRootDescriptorTable(0, self.srvGpuHandle_);

            // 🚀 正确：只画 3 个顶点
            cmd->DrawInstanced(3, 1, 0, 0);
        }


    } // namespace OFFSCREEN
} // namespace HIKARI
