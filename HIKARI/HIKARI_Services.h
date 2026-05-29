#pragma once

#include "HIKARI_Core.h"
#include "HIKARI_2D.h"
#include "HIKARI_3D.h"

#include "HIKARI_Bgm.h"
#include "HIKARI_SE.h"
#include "HIKARI_Anim.h"
#include "HIKARI_Input.h"
#include "Core/HIKARI_TimeService.h"
#include "HIKARI_Particle.h"
#include "HIKARI_ParticleLab.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"

#include "Platform/HIKARI_Win32Window.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Audio/HIKARI_Audio.h"
#if defined(_DEBUG)
#include "Editor/HIKARI_EditorStyle.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "imgui.h"
#include "../ThirdParty/imgui/imgui_impl_dx12.h"
#include "../ThirdParty/imgui/imgui_impl_win32.h"
#endif
#include <objbase.h>
#include <sstream>

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

        inline void ConfigureEditorImGuiContext() {
#if defined(_DEBUG)
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigDockingWithShift = false;
            io.ConfigDockingTransparentPayload = true;
            io.ConfigViewportsNoAutoMerge = false;
            io.ConfigViewportsNoTaskBarIcon = false;
            EDITOR::ApplyEditorStyle();
#endif
        }

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

            const UINT imguiFontSrvIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::ImGuiFont);
            gImGuiFontSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, imguiFontSrvIndex);
            gImGuiFontSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, imguiFontSrvIndex);

            if (!ImGui_ImplWin32_Init(gWindow.GetHWND())) {
                HIKARI_LOG_ERROR("ImGui_ImplWin32_Init failed.");
                return;
            }

            if (!ImGui_ImplDX12_Init(
                device,
                2,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                srvHeap,
                gImGuiFontSrvCpu,
                gImGuiFontSrvGpu)) {
                HIKARI_LOG_ERROR("ImGui_ImplDX12_Init failed.");
                ImGui_ImplWin32_Shutdown();
                return;
            }

            gImGuiBackendInitialized = true;
            HIKARI_LOG_INFO("ImGui backend initialized.");
#endif
        }

        inline bool Initialize(const char* title, const BootstrapConfig& cfg = {}) {
            CORE::InitializeLogger();
            HIKARI_LOG_INFO("HIKARI boot started.");

            gEnableImGui = cfg.enableImGui;
            gEnableEditorUI = cfg.enableEditorUI;

            HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            gComInitialized = SUCCEEDED(coHr);
            if (gComInitialized) {
                HIKARI_LOG_INFO("COM initialized.");
            }
            else {
                std::ostringstream oss;
                oss << "COM initialization failed. hr=0x" << std::hex << static_cast<unsigned long>(coHr);
                HIKARI_LOG_ERROR(oss.str());
            }

            wchar_t wTitle[256]{};
            mbstowcs_s(nullptr, wTitle, title, _TRUNCATE);

            HIKARI_LOG_INFO("Window initialization started.");
            if (!gWindow.Initialize(wTitle, cfg.windowWidth, cfg.windowHeight, cfg.resizableWindow)) {
                HIKARI_LOG_ERROR("Window initialization failed.");
                return false;
            }
            {
                std::ostringstream oss;
                oss << "Window initialized. size=" << cfg.windowWidth << "x" << cfg.windowHeight
                    << " resizable=" << (cfg.resizableWindow ? "true" : "false");
                HIKARI_LOG_INFO(oss.str());
            }
            HIKARI_LOG_INFO("D3D12 core initialization started.");
            if (!gCore.Initialize(gWindow.GetHWND(), cfg.windowWidth, cfg.windowHeight, cfg.enableDebugLayer)) {
                HIKARI_LOG_ERROR("D3D12 core initialization failed.");
                return false;
            }
            HIKARI_LOG_INFO("D3D12 core initialized.");

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

            DXTEX::DxTextureManager::Init(gCtx);
            HIKARI_LOG_INFO("TextureManager initialized.");
            HIKARI::DefaultPbrResources::Initialize();
            HIKARI_LOG_INFO("Default PBR resources initialized.");
            DX::DxRenderer::Init(gCtx);
            HIKARI_LOG_INFO("DxRenderer initialized.");
            POST::PostSystem::Initialize(gCtx);
            HIKARI_LOG_INFO("PostSystem initialized.");
            if (AUDIO::Initialize(AUDIO::BackendType::XAudio2)) {
                HIKARI_LOG_INFO("Audio initialized.");
            }
            else {
                HIKARI_LOG_WARN("Audio initialized with Null fallback.");
            }
            HIKARI::VFX::Initialize(gCtx);
            HIKARI_LOG_INFO("VFX initialized.");

            if (cfg.inputConfigPath) {
                HIKARI::HINPUT::Init(cfg.inputConfigPath);
                std::ostringstream oss;
                oss << "Input initialized. config=" << cfg.inputConfigPath;
                HIKARI_LOG_INFO(oss.str());
            }
            else {
                HIKARI::HINPUT::Init();
                HIKARI_LOG_INFO("Input initialized. config=<default>");
            }
            HIKARI::HINPUT::SetHostWindow(gWindow.GetHWND());
            HIKARI::HINPUT::SetBackend(HIKARI::HINPUT::BackendType::Win32);

            HIKARI::CAMERA::SetScreenSize(cfg.windowWidth, cfg.windowHeight);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f,0.0f });
            HIKARI::CAMERA::EnableDebugControl(cfg.enableDebugCamera);
            HIKARI_LOG_INFO("Camera initialized.");

            if (gEnableImGui && !gImGuiInitialized) {
#if defined(_DEBUG)
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ConfigureEditorImGuiContext();

                ImGuiIO& io = ImGui::GetIO();
                if (io.Fonts && io.Fonts->Fonts.empty()) {
                    io.Fonts->AddFontDefault();
                    io.Fonts->Build();
                }
                gImGuiInitialized = true;
                HIKARI_LOG_INFO("ImGui context initialized.");
#else
                gEnableImGui = false;
                gEnableEditorUI = false;
#endif
            }
            if (gEnableImGui) {
                InitializeImGuiBackend();
            }
            HIKARI_LOG_INFO("HIKARI boot completed.");
            return true;
        }

        inline void FinalizeAll() {
            HIKARI_LOG_INFO("HIKARI shutdown started.");
            if (gImGuiInitialized) {
#if defined(_DEBUG)
                if (gImGuiBackendInitialized) {
                    ImGui_ImplDX12_Shutdown();
                    ImGui_ImplWin32_Shutdown();
                    gImGuiBackendInitialized = false;
                    HIKARI_LOG_INFO("ImGui backend shutdown.");
                }
                ImGui::DestroyContext();
#endif
                gImGuiInitialized = false;
                HIKARI_LOG_INFO("ImGui shutdown.");
            }
            gImGuiFrameBegun = false;
            HIKARI::POST::PostSystem::Shutdown();
            HIKARI_LOG_INFO("PostSystem shutdown.");
            DX::DxRenderer::Finalize();
            HIKARI_LOG_INFO("DxRenderer finalized.");
            HIKARI::DefaultPbrResources::Shutdown();
            HIKARI_LOG_INFO("Default PBR resources finalized.");
            DXTEX::DxTextureManager::Finalize();
            HIKARI_LOG_INFO("TextureManager finalized.");
            HIKARI::VFX::Shutdown();
            HIKARI_LOG_INFO("VFX shutdown.");
            AUDIO::Shutdown();
            HIKARI_LOG_INFO("Audio shutdown.");
            gCore.Shutdown();
            HIKARI_LOG_INFO("D3D12 core shutdown.");
            gWindow.Shutdown();
            HIKARI_LOG_INFO("Window shutdown.");
            if (gComInitialized) {
                CoUninitialize();
                gComInitialized = false;
                HIKARI_LOG_INFO("COM uninitialized.");
            }
            HIKARI_LOG_INFO("HIKARI shutdown completed.");
            CORE::ShutdownLogger();
        }

        inline bool PumpMessages() {
            return gWindow.PumpMessages();
        }

        inline void BeginFrame(const BootstrapConfig& cfg = {}) {
            (void)cfg;
            const FrameContext& frame = HIKARI::TIME::BeginFrame();
            gCtx = gCore.BuildContext();
            DXTEX::DxTextureManager::UpdateContext(gCtx);
            DX::DxRenderer::UpdateContext(gCtx);
            POST::PostSystem::UpdateContext(gCtx);
            HIKARI::VFX::UpdateContext(gCtx);

            gCore.BeginFrame(0.05f, 0.08f, 0.12f, 1.0f);

            HIKARI::RENDERER::BeginFrame();
            HIKARI::POST::PostSystem::UpdateCommonParams(frame.gameDt);
#if defined(_DEBUG)
            if (!IsEditorUIEnabled()) {
                HIKARI::EDITOR::ClearGameViewportInputRect();
                HIKARI::POST::PostSystem::SetSceneCaptureSize(0, 0);
            }
#endif
            HIKARI::POST::PostSystem::BeginSceneCapture();

            DX::DxRenderer::BeginFrame();
            HIKARI::HINPUT::SetExternalMouseWheelDelta(gWindow.ConsumeMouseWheelDelta());
            HIKARI::HINPUT::Update(frame.unscaledDt);
            HIKARI::VFX::BeginFrame(frame.gameDt);
            if (gEnableImGui && gImGuiInitialized) {
#if defined(_DEBUG)
                if (!ImGui::GetCurrentContext()) {
                    ImGui::CreateContext();
                    ConfigureEditorImGuiContext();
                }

                ConfigureEditorImGuiContext();

                if (!gImGuiBackendInitialized) {
                    InitializeImGuiBackend();
                }

                if (gImGuiBackendInitialized) {
                    ImGui_ImplDX12_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                }

                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2(static_cast<float>(gWindow.Width()), static_cast<float>(gWindow.Height()));
                io.DeltaTime = (frame.unscaledDt > 0.0f) ? frame.unscaledDt : (1.0f / 60.0f);
                if (io.Fonts && io.Fonts->Fonts.empty()) {
                    io.Fonts->AddFontDefault();
                    io.Fonts->Build();
                }
                ImGui::NewFrame();
                gImGuiFrameBegun = true;
#endif
            }
            HIKARI::CAMERA::Update(frame.gameDt);
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

                ImGuiIO& io = ImGui::GetIO();
                if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
#endif
            }

            gCore.EndFrame();
        }
    } // namespace SERVICES
} // namespace HIKARI
