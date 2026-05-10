#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {
    namespace POST {

        // 混合模式枚举
        enum class BlendOption {
            Alpha,    // 普通透明混合 (SrcAlpha, InvSrcAlpha)
            Additive, // 加法混合 (SrcAlpha, One) - 用于绘制光源本身
            Multiply  // [新增] 乘法混合 (DestColor, Zero) - 用于最终光照合成 (Scene * Light)
        };

        class QuadDrawer
        {
        public:
            QuadDrawer() = default;
            ~QuadDrawer() { Finalize(); }

            bool Init(const GFX::Context& ctx);
            void UpdateContext(const GFX::Context& ctx);
            void Finalize();

            void SetOutputFormat(DXGI_FORMAT format);
            void DrawFullscreen(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu);
            void SetInputTexture(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu);
            void SetPixelShader(ID3DBlob* psBlob);
            void SetConstantBuffer(D3D12_GPU_VIRTUAL_ADDRESS cbv0);
            void DrawFullscreen();

            // 支持混合模式的绘制
            void DrawBlended(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu, BlendOption mode = BlendOption::Alpha);

        private:
            bool CreateRootSignature();
            bool CreatePipeline(ID3DBlob* psBlob, Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPso);
            bool CreateBlendPipelines();
        

        private:
            bool initialized_ = false;
            Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
            Microsoft::WRL::ComPtr<ID3DBlob> vsBlob_;
            Microsoft::WRL::ComPtr<ID3DBlob> psCopyBlob_;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> psoCopy_;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> psoPost_;

            // 混合管线
            Microsoft::WRL::ComPtr<ID3D12PipelineState> psoBlendAlpha_;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> psoBlendAdd_;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> psoBlendMultiply_; 

            ID3DBlob* currentPostPS_ = nullptr;
            DXGI_FORMAT outputFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;
            ID3D12DescriptorHeap* currentSrvHeap_ = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE currentSrvGpu_{};
            D3D12_GPU_VIRTUAL_ADDRESS currentCBV0_ = 0;
            GFX::Context context_{};
        };

    } // POST
} // HIKARI
