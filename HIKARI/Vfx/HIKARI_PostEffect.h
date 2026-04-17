#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include "HIKARI_D3DBlobCompat.h"
#include "HIKARI_PostCommon.h"

namespace HIKARI {
    namespace POST {

        class QuadDrawer;

        class PostEffect
        {
        public:
            PostEffect();
            ~PostEffect();

            bool LoadPixelShader(const wchar_t* path);

            void ApplyCommonParams(const CommonParams& p); 

            void SetTime(float t);
            void SetUser(int index, const DirectX::XMFLOAT4& v);

            void BindAndDraw(QuadDrawer& drawer);

        private:
            bool CreateConstantBuffer();

        private:
            Microsoft::WRL::ComPtr<ID3DBlob> psBlob_;

            Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
            void* mappedPtr_ = nullptr;

            CommonParams params_{};
        };

    } // POST
} // HIKARI
