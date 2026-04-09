#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <d3dx12.h>

#include "KamataEngine.h"

namespace HIKARI {
    namespace OFFSCREEN {

        /// <summary>
        /// シンプルなオフスクリーンレンダリング管理クラス
        /// - Init で一度だけ初期化
        /// - BeginScene / EndScene で「このフレームはオフスクリーンに描く」
        /// - DrawToScreen でオフスクリーンを全画面に貼り付ける
        /// </summary>
        class Offscreen {
        public:
            /// <summary>
            /// 初期化：width/height はオフスクリーンのサイズ
            /// ふつうはウィンドウサイズと同じで OK
            /// </summary>
             void Init(int width, int height);

            /// <summary>
            /// リソース解放
            /// </summary>
             void Finalize();

            /// <summary>
            /// 以降の描画をオフスクリーンに向ける
            /// （OMSetRenderTargets + Clear）
            /// </summary>
           void BeginScene();

            /// <summary>
            /// オフスクリーンでの描画終了
            /// （リソースステートを PIXEL_SHADER_RESOURCE に遷移）
            /// </summary>
            void EndScene();

            /// <summary>
            /// オフスクリーンの内容をバックバッファに全画面描画する
            /// </summary>
            void DrawToScreen();

            /// <summary>
            /// 初期化済みかどうか
            /// </summary>
            bool IsInitialized();

            int GetTextureHandle();

        private:


            static Offscreen& Instance();

            void CreateResources(int width, int height);
            void DestroyResources();
            void CreatePipeline();

        private:
            KamataEngine::DirectXCommon* dxCommon_ = nullptr;

            // オフスクリーン用カラーターゲット
            Microsoft::WRL::ComPtr<ID3D12Resource> colorTex_;
            D3D12_RESOURCE_STATES colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

            // RTV / SRV 用ヒープ
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle_{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};

            // フルスクリーン描画用パイプライン
            Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

            // 描画用ビューポート / シザー
            D3D12_VIEWPORT viewport_{};
            D3D12_RECT scissorRect_{};

            int width_ = 0;
            int height_ = 0;
            bool initialized_ = false;
            int textureHandle_ = -1;
        };

    } // namespace OFFSCREEN
} // namespace HIKARI
