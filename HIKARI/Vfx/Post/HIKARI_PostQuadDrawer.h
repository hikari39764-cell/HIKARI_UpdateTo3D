#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {
    namespace POST {

        enum class BlendOption {
            Alpha,
            Additive,
            Multiply
        };

        class QuadDrawer
        {
        public:
            QuadDrawer() = default;
            ~QuadDrawer() { Finalize(); }

            bool Init(const GFX::Context& ctx);
            void UpdateContext(const GFX::Context& ctx);
            void Finalize();
            std::string DumpState() const;
            const char* GetOutputFormatName() const;

            void DrawFullscreen(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu);
            void SetInputTexture(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu);
            bool SetPixelShader(ID3DBlob* psBlob);
            void SetConstantBuffer(D3D12_GPU_VIRTUAL_ADDRESS cbv0);
            void DrawFullscreen();
            void DrawBlended(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu, BlendOption mode = BlendOption::Alpha);

        private:
            bool CreateRootSignature();
            bool CreatePipeline(ID3DBlob* psBlob, Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPso, const char* debugName);
            bool CreateBlendPipelines();

        private:
            bool initialized_ = false;
            Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
            Microsoft::WRL::ComPtr<ID3DBlob> vsBlob_;
            Microsoft::WRL::ComPtr<ID3DBlob> psCopyBlob_;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> psoCopy_;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> psoPost_;
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

    } // namespace POST
} // namespace HIKARI
