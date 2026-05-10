#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include <Windows.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <cstring>
#include <d3dx12.h>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace POST {

        GFX::Context PostEffect::context_{};

        PostEffect::PostEffect()
        {
            ZeroMemory(&params_, sizeof(params_));
        }

        void PostEffect::UpdateContext(const GFX::Context& ctx) {
            context_ = ctx;
        }

        PostEffect::~PostEffect()
        {
            if (constantBuffer_ && mappedPtr_) {
                constantBuffer_->Unmap(0, nullptr);
                mappedPtr_ = nullptr;
            }
        }

        bool PostEffect::CreateConstantBuffer()
        {
            if (constantBuffer_ && mappedPtr_) {
                return true;
            }

            auto* device = context_.device;
            if (!device) {
                OutputDebugStringA("[PostEffect] context_.device is null; postpone constant buffer creation.\n");
                return false;
            }

            UINT size = Align256(sizeof(CommonParams));

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
            CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

            HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &resDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(constantBuffer_.GetAddressOf())
            );
            if (FAILED(hr)) {
                OutputDebugStringA("[PostEffect] CreateCommittedResource failed.\n");
                return false;
            }

            hr = constantBuffer_->Map(0, nullptr, &mappedPtr_);
            if (FAILED(hr)) {
                OutputDebugStringA("[PostEffect] Constant buffer map failed.\n");
                constantBuffer_.Reset();
                mappedPtr_ = nullptr;
                return false;
            }
            return true;
        }
        bool PostEffect::LoadPixelShader(const wchar_t* path)
        {
            ComPtr<ID3DBlob> err;

            HRESULT hr = D3DCompileFromFile(
                path,
                nullptr,
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "main",
                "ps_5_0",
                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                0,
                psBlob_.GetAddressOf(),
                err.GetAddressOf()
            );

            if (FAILED(hr)) {
                if (err) {
                    OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                }
                return false;
            }

            return true;
        }

        void PostEffect::ApplyCommonParams(const CommonParams& p)
        {
            params_ = p;
        }

        void PostEffect::SetTime(float t)
        {
            params_.time = t;
        }


        void PostEffect::SetUser(int index, const DirectX::XMFLOAT4& v)
        {
            if (index < 0 || index >= 16) { return; }
            params_.user[index] = v;
        }

        void PostEffect::BindAndDraw(QuadDrawer& drawer)
        {
            if (!psBlob_) { return; }
            if (!mappedPtr_ && !CreateConstantBuffer()) {
                return;
            }
            if (!constantBuffer_) {
                return;
            }

            memcpy(mappedPtr_, &params_, sizeof(params_));

            drawer.SetPixelShader(psBlob_.Get());
            drawer.SetConstantBuffer(constantBuffer_->GetGPUVirtualAddress());
            drawer.DrawFullscreen();
        }

    } // POST
} // HIKARI
