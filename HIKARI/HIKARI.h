#pragma once
#include "HIKARI_Texture.h"
#include "HIKARI_Renderer.h"
#include "HIKARI_Camera.h"
#include "HIKARI_Transform2D.h"
#include "HIKARI_Bgm.h"
#include "HIKARI_SE.h"
#include "HIKARI_Anim.h"
#include "HIKARI_SpineTextureLoader.h"
#include "HIKARI_SpineActor.h"
#include "HIKARI_Input.h"
#include "HIKARI_Particle.h"
#include "HIKARI_SpriteAnimator.h"
#include "HIKARI_MultiTextureAnimator.h"
#include "HIKARI_Utility.h"
#include "HIKARI_ParticleLab.h"
#include "HIKARI_Collision.h"
#include "HIKARI_CollisionWorld.h"
#include "HIKARI_MapRoom.h"
#include "HIKARI_PhysicsWorld.h"
#include "HIKARI_Body2D.h"
#include "HIKARI_TiledCollision.h"
#include "HIKARI_Offscreen.h"
#include "HIKARI_DxRenderer.h"
#include "HIKARI_DxTexture.h"
#include "HIKARI_MeshEffect.h"
#include "HIKARI_PostSystem.h"
#include "HIKARI_PostEffect.h"
#include "HIKARI_PostQuadDrawer.h"
#include "HIKARI_PostChain.h"

namespace HIKARI {
    namespace SERVICES {

        struct BootstrapConfig {
            const char* inputConfigPath = "input.json"; // キーボード/マウス/Pad 設定
            bool enableDebugCamera = true;               // デバッグ用カメラ操作
            //bool drawScanlineBackground = true;          // スキャンライン背景の描画
        };

        // サービス層の初期化/終了
        inline void Initialize(const char* title, const BootstrapConfig& cfg = {}) {
            Novice::Initialize(title, kScreenW, kScreenH);
            DXTEX::DxTextureManager::Init(1024);
            DX::DxRenderer::Init();
            HIKARI::POST::PostSystem::Initialize();

            if (cfg.inputConfigPath) {
                HIKARI::HINPUT::Init(cfg.inputConfigPath);
            } else {
                HIKARI::HINPUT::Init();
            }

            HIKARI::CAMERA::SetScreenSize(kScreenW, kScreenH);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f,0.0f });
            HIKARI::CAMERA::EnableDebugControl(cfg.enableDebugCamera);
        }
        \
        inline void FinalizeAll() {
            HIKARI::POST::PostSystem::Shutdown();
            DX::DxRenderer::Finalize();
            DXTEX::DxTextureManager::Finalize();
            Novice::Finalize();
        }

        // 1 フレームの開始/終了（サービス層側）
        inline void BeginFrame(const BootstrapConfig& cfg = {}) {
            Novice::BeginFrame();

            // 1.清空渲染命令列表
            HIKARI::RENDERER::BeginFrame();
            HIKARI::POST::PostSystem::UpdateCommonParams(kDt);
            HIKARI::POST::PostSystem::BeginSceneCapture();

            // 3. 重置底层 DX 渲染器状态
            DX::DxRenderer::BeginFrame();

            // 4. 更新输入和摄像机
            HIKARI::HINPUT::Update(kDt);
            HIKARI::CAMERA::Update(kDt);
        }

        inline void EndFrame() {
            HIKARI::RENDERER::RenderAll();
            HIKARI::POST::PostSystem::EndSceneCaptureAndPresent();
            Novice::EndFrame();
        }
    } // namespace SERVICES
} // namespace HIKARI
