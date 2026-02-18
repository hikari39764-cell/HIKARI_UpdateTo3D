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

#include "Platform/HIKARI_Win32Window.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Audio/HIKARI_Audio.h"

namespace HIKARI {
    namespace SERVICES {

        struct BootstrapConfig {
            const char* inputConfigPath = "input.json";
            bool enableDebugCamera = true;
            bool enableDebugLayer = true;
            bool resizableWindow = true;
            int windowWidth = kScreenW;
            int windowHeight = kScreenH;
        };

        inline PLATFORM::Win32Window gWindow{};
        inline GFX::Dx12Core gCore{};
        inline GFX::Context gCtx{};

        inline bool Initialize(const char* title, const BootstrapConfig& cfg = {}) {
            wchar_t wTitle[256]{};
            mbstowcs_s(nullptr, wTitle, title, _TRUNCATE);

            if (!gWindow.Initialize(wTitle, cfg.windowWidth, cfg.windowHeight, cfg.resizableWindow)) {
                return false;
            }
            if (!gCore.Initialize(gWindow.GetHWND(), cfg.windowWidth, cfg.windowHeight, cfg.enableDebugLayer)) {
                return false;
            }

            gWindow.SetResizeCallback([](int w, int h) {
                gCore.Resize(w, h);
                gCtx = gCore.BuildContext();
                DXTEX::DxTextureManager::UpdateContext(gCtx);
                DX::DxRenderer::UpdateContext(gCtx);
                POST::PostSystem::UpdateContext(gCtx);
            });

            gCtx = gCore.BuildContext();

            DXTEX::DxTextureManager::Init(gCtx, 1024);
            DX::DxRenderer::Init(gCtx);
            POST::PostSystem::Initialize(gCtx);
            AUDIO::Initialize(AUDIO::BackendType::Novice);

            if (cfg.inputConfigPath) {
                HIKARI::HINPUT::Init(cfg.inputConfigPath);
            }
            else {
                HIKARI::HINPUT::Init();
            }
            HIKARI::HINPUT::SetHostWindow(gWindow.GetHWND());
            HIKARI::HINPUT::SetBackend(HIKARI::HINPUT::BackendType::Win32);

            HIKARI::CAMERA::SetScreenSize(cfg.windowWidth, cfg.windowHeight);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f,0.0f });
            HIKARI::CAMERA::EnableDebugControl(cfg.enableDebugCamera);
            return true;
        }

        inline void FinalizeAll() {
            HIKARI::POST::PostSystem::Shutdown();
            DX::DxRenderer::Finalize();
            DXTEX::DxTextureManager::Finalize();
            AUDIO::Shutdown();
            gCore.Shutdown();
            gWindow.Shutdown();
        }

        inline bool PumpMessages() {
            return gWindow.PumpMessages();
        }

        inline void BeginFrame(const BootstrapConfig& cfg = {}) {
            (void)cfg;
            gCtx = gCore.BuildContext();
            DXTEX::DxTextureManager::UpdateContext(gCtx);
            DX::DxRenderer::UpdateContext(gCtx);
            POST::PostSystem::UpdateContext(gCtx);

            gCore.BeginFrame(0.05f, 0.08f, 0.12f, 1.0f);

            HIKARI::RENDERER::BeginFrame();
            HIKARI::POST::PostSystem::UpdateCommonParams(kDt);
            HIKARI::POST::PostSystem::BeginSceneCapture();

            DX::DxRenderer::BeginFrame();
            HIKARI::HINPUT::Update(kDt);
            HIKARI::CAMERA::Update(kDt);
        }

        inline void EndFrame() {
            HIKARI::RENDERER::RenderAll();
            HIKARI::POST::PostSystem::EndSceneCaptureAndPresent();
            gCore.EndFrame();
        }
    } // namespace SERVICES
} // namespace HIKARI
