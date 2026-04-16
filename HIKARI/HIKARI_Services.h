#pragma once

#include "HIKARI_Core.h"
#include "HIKARI_2D.h"
#include "HIKARI_3D.h"

#include "HIKARI_Bgm.h"
#include "HIKARI_SE.h"
#include "HIKARI_Anim.h"
#include "HIKARI_Input.h"
#include "HIKARI_Particle.h"
#include "HIKARI_SpriteAnimator.h"
#include "HIKARI_MultiTextureAnimator.h"
#include "HIKARI_ParticleLab.h"
#include "HIKARI_Collision.h"
#include "HIKARI_CollisionWorld.h"
#include "HIKARI_MapRoom.h"
#include "HIKARI_PhysicsWorld.h"
#include "HIKARI_Body2D.h"
#include "HIKARI_TiledCollision.h"
#include "Vfx/HIKARI_VfxSystem.h"

#include "Platform/HIKARI_Win32Window.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Audio/HIKARI_Audio.h"
#if defined(_DEBUG)
#include "imgui.h"
#include "../ThirdParty/imgui/imgui_impl_dx12.h"
#include "../ThirdParty/imgui/imgui_impl_win32.h"
#endif
#include <objbase.h>

namespace HIKARI {
    namespace SERVICES {

        struct BootstrapConfig {
            const char* inputConfigPath = "input.json";
            bool enableDebugCamera = false;
            bool enableDebugLayer = true;
            bool resizableWindow = true;
            bool enableImGui =
#if defined(_DEBUG)
                true;
#else
                false;
#endif
            bool enableEditorUI =
#if defined(_DEBUG)
                true;
#else
                false;
#endif
            int windowWidth = kScreenW;
            int windowHeight = kScreenH;
        };

        inline PLATFORM::Win32Window gWindow{};
        inline GFX::Dx12Core gCore{};
        inline GFX::Context gCtx{};
        inline bool gComInitialized = false;
        inline bool gImGuiInitialized = false;
        inline bool gImGuiBackendInitialized = false;
        inline bool gImGuiFrameBegun = false;
        inline bool gEnableImGui = false;
        inline bool gEnableEditorUI = false;
        inline D3D12_CPU_DESCRIPTOR_HANDLE gImGuiFontSrvCpu{};
        inline D3D12_GPU_DESCRIPTOR_HANDLE gImGuiFontSrvGpu{};

        inline bool IsImGuiEnabled() { return gEnableImGui; }
        inline bool IsEditorUIEnabled() { return gEnableImGui && gEnableEditorUI; }
        inline void SetEditorUIEnabled(bool enabled) { gEnableEditorUI = enabled; }

        inline void InitializeImGuiBackend() {
#if !defined(_DEBUG)
            return;
#else
            if (gImGuiBackendInitialized) {
                return;
            }

            auto* device = gCtx.device;
            auto* srvHeap = gCtx.srvHeap;
            if (!device || !srvHeap || !gWindow.GetHWND()) {
                return;
            }

            const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            auto cpuStart = srvHeap->GetCPUDescriptorHandleForHeapStart();
            auto gpuStart = srvHeap->GetGPUDescriptorHandleForHeapStart();

            constexpr UINT kImGuiFontSrvIndex = 2047;
            gImGuiFontSrvCpu.ptr = cpuStart.ptr + static_cast<SIZE_T>(descriptorSize) * kImGuiFontSrvIndex;
            gImGuiFontSrvGpu.ptr = gpuStart.ptr + static_cast<UINT64>(descriptorSize) * kImGuiFontSrvIndex;

            if (!ImGui_ImplWin32_Init(gWindow.GetHWND())) {
                OutputDebugStringA("[ImGui] ImGui_ImplWin32_Init failed.\n");
                return;
            }

            if (!ImGui_ImplDX12_Init(
                device,
                2,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                srvHeap,
                gImGuiFontSrvCpu,
                gImGuiFontSrvGpu)) {
                OutputDebugStringA("[ImGui] ImGui_ImplDX12_Init failed.\n");
                ImGui_ImplWin32_Shutdown();
                return;
            }

            gImGuiBackendInitialized = true;
#endif
        }

        inline bool Initialize(const char* title, const BootstrapConfig& cfg = {}) {
            gEnableImGui = cfg.enableImGui;
            gEnableEditorUI = cfg.enableEditorUI;

            HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            gComInitialized = SUCCEEDED(coHr);

            wchar_t wTitle[256]{};
            mbstowcs_s(nullptr, wTitle, title, _TRUNCATE);

            if (!gWindow.Initialize(wTitle, cfg.windowWidth, cfg.windowHeight, cfg.resizableWindow)) {
                return false;
            }
            if (!gCore.Initialize(gWindow.GetHWND(), cfg.windowWidth, cfg.windowHeight, cfg.enableDebugLayer)) {
                return false;
            }

            const int logicalScreenW = cfg.windowWidth;
            const int logicalScreenH = cfg.windowHeight;

            gWindow.SetResizeCallback([logicalScreenW, logicalScreenH](int w, int h) {
                gCore.Resize(w, h);
                gCtx = gCore.BuildContext();
                DXTEX::DxTextureManager::UpdateContext(gCtx);
                DX::DxRenderer::UpdateContext(gCtx);
                POST::PostSystem::UpdateContext(gCtx);
                HIKARI::VFX::UpdateContext(gCtx);

                HIKARI::CAMERA::SetScreenSize(logicalScreenW, logicalScreenH);
                HIKARI::CAMERA::SetScreenCenter({ 0.0f, 0.0f });
            });

            gCtx = gCore.BuildContext();

            DXTEX::DxTextureManager::Init(gCtx, 1024);
            DX::DxRenderer::Init(gCtx);
            POST::PostSystem::Initialize(gCtx);
            AUDIO::Initialize(AUDIO::BackendType::Kamata);
            HIKARI::VFX::Initialize(gCtx);

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

            if (gEnableImGui && !gImGuiInitialized) {
#if defined(_DEBUG)
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGui::StyleColorsDark();

                ImGuiIO& io = ImGui::GetIO();
                if (io.Fonts && io.Fonts->Fonts.empty()) {
                    io.Fonts->AddFontDefault();
                    io.Fonts->Build();
                }
                gImGuiInitialized = true;
#else
                gEnableImGui = false;
                gEnableEditorUI = false;
#endif
            }
            if (gEnableImGui) {
                InitializeImGuiBackend();
            }
            return true;
        }

        inline void FinalizeAll() {
            if (gImGuiInitialized) {
#if defined(_DEBUG)
                if (gImGuiBackendInitialized) {
                    ImGui_ImplDX12_Shutdown();
                    ImGui_ImplWin32_Shutdown();
                    gImGuiBackendInitialized = false;
                }
                ImGui::DestroyContext();
#endif
                gImGuiInitialized = false;
            }
            gImGuiFrameBegun = false;
            HIKARI::POST::PostSystem::Shutdown();
            DX::DxRenderer::Finalize();
            DXTEX::DxTextureManager::Finalize();
            HIKARI::VFX::Shutdown();
            AUDIO::Shutdown();
            gCore.Shutdown();
            gWindow.Shutdown();
            if (gComInitialized) {
                CoUninitialize();
                gComInitialized = false;
            }
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
            HIKARI::VFX::UpdateContext(gCtx);

            gCore.BeginFrame(0.05f, 0.08f, 0.12f, 1.0f);

            HIKARI::RENDERER::BeginFrame();
            HIKARI::POST::PostSystem::UpdateCommonParams(kDt);
            HIKARI::POST::PostSystem::BeginSceneCapture();

            DX::DxRenderer::BeginFrame();
            HIKARI::HINPUT::SetExternalMouseWheelDelta(gWindow.ConsumeMouseWheelDelta());
            HIKARI::HINPUT::Update(kDt);
            HIKARI::VFX::BeginFrame(kDt);
            if (gEnableImGui && gImGuiInitialized) {
#if defined(_DEBUG)
                if (!gImGuiBackendInitialized) {
                    InitializeImGuiBackend();
                }

                if (gImGuiBackendInitialized) {
                    ImGui_ImplDX12_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                }

                if (!ImGui::GetCurrentContext()) {
                    ImGui::CreateContext();
                }

                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2(static_cast<float>(gWindow.Width()), static_cast<float>(gWindow.Height()));
                io.DeltaTime = (kDt > 0.0f) ? kDt : (1.0f / 60.0f);
                if (io.Fonts && io.Fonts->Fonts.empty()) {
                    io.Fonts->AddFontDefault();
                    io.Fonts->Build();
                }
                ImGui::NewFrame();
                gImGuiFrameBegun = true;
#endif
            }
            HIKARI::CAMERA::Update(kDt);
        }

        inline void EndFrame() {
            HIKARI::VFX::EndFrame();
            if (gEnableImGui && gImGuiInitialized && gImGuiFrameBegun) {
#if defined(_DEBUG)
                ImGui::Render();
                gImGuiFrameBegun = false;
#endif
            }
            HIKARI::RENDERER::RenderLayerRange(HIKARI::RENDERER::RenderLayer::Background, HIKARI::RENDERER::RenderLayer::VFX, false);
            HIKARI::POST::PostSystem::EndSceneCaptureAndPresent();
            HIKARI::RENDERER::RenderLayerRange(HIKARI::RENDERER::RenderLayer::UI, HIKARI::RENDERER::RenderLayer::Debug, true);

            if (gEnableImGui && gImGuiInitialized && gImGuiBackendInitialized) {
#if defined(_DEBUG)
                auto* cmd = gCtx.cmdList;
                ID3D12DescriptorHeap* heaps[] = { gCtx.srvHeap };
                cmd->SetDescriptorHeaps(1, heaps);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
#endif
            }

            gCore.EndFrame();
        }
    } // namespace SERVICES
} // namespace HIKARI
