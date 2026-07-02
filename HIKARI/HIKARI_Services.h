#pragma once

#include "HIKARI_Core.h"
#include "HIKARI_2D.h"
#include "HIKARI_3D.h"

#include "HIKARI_Bgm.h"
#include "HIKARI_SE.h"
#include "HIKARI_Anim.h"
#include "HIKARI_Input.h"
#include "Core/HIKARI_TimeService.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"

#include "Platform/HIKARI_Win32Window.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/HIKARI_RenderResourceDescriptorPool.h"
#include "Audio/HIKARI_Audio.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Style/HIKARI_EditorIconManager.h"
#include "Editor/HIKARI_EditorStyle.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#endif
#if defined(HIKARI_ENABLE_IMGUI)
#include "imgui.h"
#include "../ThirdParty/imgui/imgui_impl_dx12.h"
#include "../ThirdParty/imgui/imgui_impl_win32.h"
#endif
#include <objbase.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace HIKARI {
    namespace SERVICES {

        struct BootstrapConfig {
            RuntimeHostMode hostMode = DefaultRuntimeHostMode();
            const char* inputConfigPath = "input.json";
            bool enableDebugCamera = false;
            bool enableDebugLayer = true;
            bool resizableWindow = true;
            bool enableImGui =
#if defined(HIKARI_ENABLE_IMGUI)
                true;
#else
                false;
#endif
            bool enableEditorUI =
#if defined(HIKARI_WITH_EDITOR)
                true;
#else
                false;
#endif
            bool enablePortableObjectTools = false;
            int windowWidth = kScreenW;
            int windowHeight = kScreenH;
            std::string startupSceneGuid{};
            std::vector<std::string> exportedSceneGuids{};
        };

        inline PLATFORM::Win32Window gWindow{};
        inline GFX::Dx12Core gCore{};
        inline GFX::Context gCtx{};
        inline bool gComInitialized = false;
        inline bool gImGuiInitialized = false;
        inline bool gImGuiBackendInitialized = false;
        inline bool gImGuiFrameBegun = false;
        inline RuntimeHostMode gRuntimeHostMode = DefaultRuntimeHostMode();
        inline bool gEnableImGui = false;
        inline bool gEnableEditorUI = false;
        inline bool gEnablePortableObjectTools = false;
        inline std::string gRuntimeStartupSceneGuid{};
        inline std::vector<std::string> gRuntimeExportedSceneGuids{};
        inline D3D12_CPU_DESCRIPTOR_HANDLE gImGuiFontSrvCpu{};
        inline D3D12_GPU_DESCRIPTOR_HANDLE gImGuiFontSrvGpu{};
        inline bool gGpuFrameReady = false;
        inline bool gHasPendingWindowResize = false;
        inline int gPendingWindowWidth = 0;
        inline int gPendingWindowHeight = 0;
        inline int gLogicalScreenWidth = kScreenW;
        inline int gLogicalScreenHeight = kScreenH;
        inline bool gEditorGameViewportVisible = false;
        inline int gEditorGameViewportWidth = 0;
        inline int gEditorGameViewportHeight = 0;

        inline RuntimeHostMode GetRuntimeHostMode() { return gRuntimeHostMode; }
        inline bool IsEditorHost() { return IsEditorHostMode(gRuntimeHostMode); }
        inline bool IsExportedGameHost() { return IsExportedGameHostMode(gRuntimeHostMode); }
        inline bool IsImGuiEnabled() { return gEnableImGui; }
        inline bool IsEditorUIEnabled() { return gEnableImGui && gEnableEditorUI; }
        inline bool ArePortableObjectToolsEnabled() { return gEnableImGui && gEnablePortableObjectTools; }
        inline const std::string& GetRuntimeStartupSceneGuid() { return gRuntimeStartupSceneGuid; }
        inline const std::vector<std::string>& GetRuntimeExportedSceneGuids() { return gRuntimeExportedSceneGuids; }
        inline bool HasRuntimeExportedSceneFilter() { return !gRuntimeExportedSceneGuids.empty(); }
        inline bool IsRuntimeSceneGuidAllowed(const std::string& sceneGuid) {
            if (gRuntimeExportedSceneGuids.empty()) {
                return true;
            }
            return std::find(
                gRuntimeExportedSceneGuids.begin(),
                gRuntimeExportedSceneGuids.end(),
                sceneGuid) != gRuntimeExportedSceneGuids.end();
        }
        inline void SetEditorUIEnabled(bool enabled) { gEnableEditorUI = IsEditorHost() && enabled; }
        inline void SetPortableObjectToolsEnabled(bool enabled) {
            gEnablePortableObjectTools = AllowsPortableObjectTools(gRuntimeHostMode) && enabled;
        }

        inline void SetEditorGameViewportSize(int width, int height, bool visible) {
            gEditorGameViewportVisible = visible && width > 0 && height > 0;
            gEditorGameViewportWidth = gEditorGameViewportVisible ? width : 0;
            gEditorGameViewportHeight = gEditorGameViewportVisible ? height : 0;
        }

        inline void UpdateGpuContexts() {
            gCtx = gCore.BuildContext();
            DXTEX::DxTextureManager::UpdateContext(gCtx);
            RENDER3D::UpdateRenderResourceDescriptorPoolContext(gCtx);
            RENDER3D::UpdateClusterGeometryResourceContext(gCtx);
            DX::DxRenderer::UpdateContext(gCtx);
            POST::PostSystem::UpdateContext(gCtx);
            HIKARI::VFX::UpdateContext(gCtx);
        }

        inline void QueueWindowResize(int width, int height) {
            if (width <= 0 || height <= 0) {
                return;
            }
            gPendingWindowWidth = width;
            gPendingWindowHeight = height;
            gHasPendingWindowResize = true;
        }

        inline bool ApplyPendingWindowResize() {
            if (!gHasPendingWindowResize) {
                return true;
            }

            const int width = gPendingWindowWidth;
            const int height = gPendingWindowHeight;
            gHasPendingWindowResize = false;
            gPendingWindowWidth = 0;
            gPendingWindowHeight = 0;

            if (width <= 0 || height <= 0) {
                return true;
            }

            if (!gCore.Resize(width, height)) {
                gGpuFrameReady = false;
                HIKARI_LOG_ERROR("D3D12 deferred resize failed; GPU frame recording disabled.");
                return false;
            }

            UpdateGpuContexts();
            HIKARI::CAMERA::SetScreenSize(gLogicalScreenWidth, gLogicalScreenHeight);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f, 0.0f });
            return true;
        }

        inline bool ApplyWindowPresentationSettings() {
            const RENDER3D::RenderQualitySettings& settings =
                RENDER3D::GetRenderQualitySettings();
            const RENDER3D::RenderResolution size =
                RENDER3D::ResolveFixedRenderResolution(settings.windowSize);
            const int windowWidth = size.width > 0 ? size.width : gWindow.Width();
            const int windowHeight = size.height > 0 ? size.height : gWindow.Height();

            PLATFORM::WindowMode mode = PLATFORM::WindowMode::Windowed;
            switch (settings.windowMode) {
            case RENDER3D::WindowPresentationMode::BorderlessWindow:
                mode = PLATFORM::WindowMode::BorderlessWindow;
                break;
            case RENDER3D::WindowPresentationMode::Fullscreen:
                mode = PLATFORM::WindowMode::Fullscreen;
                break;
            case RENDER3D::WindowPresentationMode::Windowed:
            default:
                mode = PLATFORM::WindowMode::Windowed;
                break;
            }

            return gWindow.ApplyWindowMode(mode, windowWidth, windowHeight);
        }

        inline RENDER3D::RenderResolution ResolveFrameSceneCaptureResolution() {
            const RENDER3D::RenderQualitySettings& settings =
                RENDER3D::GetRenderQualitySettings();

            int viewportWidth = gWindow.Width();
            int viewportHeight = gWindow.Height();
#if defined(HIKARI_WITH_EDITOR)
            if (IsEditorHost() &&
                IsEditorUIEnabled() &&
                gEditorGameViewportVisible &&
                gEditorGameViewportWidth > 0 &&
                gEditorGameViewportHeight > 0) {
                viewportWidth = gEditorGameViewportWidth;
                viewportHeight = gEditorGameViewportHeight;
            }
#endif

            return RENDER3D::ResolveSceneCaptureResolution(
                settings,
                viewportWidth,
                viewportHeight);
        }

        inline void ApplyFrameSceneCaptureSize() {
            const RENDER3D::RenderResolution resolution =
                ResolveFrameSceneCaptureResolution();
            HIKARI::POST::PostSystem::SetSceneCaptureSize(
                resolution.width,
                resolution.height);
            HIKARI::DX::DxRenderer::SetScreenSize(
                resolution.width,
                resolution.height);
        }

        inline void ConfigureEditorImGuiContext() {
#if defined(HIKARI_ENABLE_IMGUI)
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigDockingWithShift = false;
            io.ConfigDockingTransparentPayload = true;
            io.ConfigViewportsNoAutoMerge = false;
            io.ConfigViewportsNoTaskBarIcon = false;
#if defined(HIKARI_WITH_EDITOR)
            EDITOR::ApplyEditorStyle();
#endif
#endif
        }

        inline void InitializeImGuiBackend() {
#if !defined(HIKARI_ENABLE_IMGUI)
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
                static_cast<int>(GFX::kFrameResourceCount),
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

            gRuntimeHostMode = cfg.hostMode;
            gEnableImGui = cfg.enableImGui;
            gEnableEditorUI = IsEditorHostMode(gRuntimeHostMode) && cfg.enableEditorUI;
            gEnablePortableObjectTools =
                AllowsPortableObjectTools(gRuntimeHostMode) && cfg.enablePortableObjectTools;
            gRuntimeStartupSceneGuid = cfg.startupSceneGuid;
            gRuntimeExportedSceneGuids = cfg.exportedSceneGuids;
            {
                std::ostringstream oss;
                oss << "Runtime host mode=" << RuntimeHostModeName(gRuntimeHostMode)
                    << " imgui=" << (gEnableImGui ? "true" : "false")
                    << " editorUI=" << (gEnableEditorUI ? "true" : "false")
                    << " portableObjectTools=" << (gEnablePortableObjectTools ? "true" : "false")
                    << " startupSceneGuid=" << (gRuntimeStartupSceneGuid.empty() ? "<project>" : gRuntimeStartupSceneGuid)
                    << " exportedSceneCount=" << gRuntimeExportedSceneGuids.size();
                HIKARI_LOG_INFO(oss.str());
            }

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
            GFX::PIX::Initialize(gWindow.GetHWND());
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
            gLogicalScreenWidth = logicalScreenW;
            gLogicalScreenHeight = logicalScreenH;

            gWindow.SetResizeCallback([](int w, int h) {
                QueueWindowResize(w, h);
            });

            gCtx = gCore.BuildContext();

            DXTEX::DxTextureManager::Init(gCtx);
            HIKARI_LOG_INFO("TextureManager initialized.");
            RENDER3D::UpdateRenderResourceDescriptorPoolContext(gCtx);
            RENDER3D::UpdateClusterGeometryResourceContext(gCtx);
#if defined(HIKARI_WITH_EDITOR)
            if (IsEditorHost() && gEnableImGui) {
                if (EDITOR::EditorIconManager::Initialize()) {
                    HIKARI_LOG_INFO("Editor icons initialized.");
                }
                else {
                    HIKARI_LOG_WARN("Editor icon initialization failed.");
                }
            }
#endif
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
#if defined(HIKARI_ENABLE_IMGUI)
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
                gEnablePortableObjectTools = false;
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
#if defined(HIKARI_ENABLE_IMGUI)
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
            RENDER3D::ShutdownClusterGeometryResourceSystem();
            RENDER3D::ShutdownRenderResourceDescriptorPool();
#if defined(HIKARI_WITH_EDITOR)
            EDITOR::EditorIconManager::Finalize();
#endif
            DXTEX::DxTextureManager::Finalize();
            HIKARI_LOG_INFO("TextureManager finalized.");
            HIKARI::VFX::Shutdown();
            HIKARI_LOG_INFO("VFX shutdown.");
            AUDIO::Shutdown();
            HIKARI_LOG_INFO("Audio shutdown.");
            gCore.Shutdown();
            HIKARI_LOG_INFO("D3D12 core shutdown.");
            GFX::PIX::Shutdown();
            gWindow.Shutdown();
            HIKARI_LOG_INFO("Window shutdown.");
            if (gComInitialized) {
                CoUninitialize();
                gComInitialized = false;
                HIKARI_LOG_INFO("COM uninitialized.");
            }
            gRuntimeStartupSceneGuid.clear();
            gRuntimeExportedSceneGuids.clear();
            HIKARI_LOG_INFO("HIKARI shutdown completed.");
            CORE::ShutdownLogger();
        }

        inline bool PumpMessages() {
            return gWindow.PumpMessages();
        }

        inline bool BeginFrame(const BootstrapConfig& cfg = {}) {
            (void)cfg;
            gGpuFrameReady = false;
            GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.BeginFrame");
            const FrameContext& frame = HIKARI::TIME::BeginFrame();
            if (!ApplyPendingWindowResize()) {
                return false;
            }
            UpdateGpuContexts();

            if (!gCore.BeginFrame(0.05f, 0.08f, 0.12f, 1.0f)) {
                HIKARI_LOG_ERROR("D3D12 BeginFrame failed; skipping frame.");
                return false;
            }
            gGpuFrameReady = true;

            HIKARI::RENDERER::BeginFrame();
            ApplyFrameSceneCaptureSize();
            HIKARI::POST::PostSystem::UpdateCommonParams(frame.gameDt);
#if defined(HIKARI_WITH_EDITOR)
            if (!IsEditorHost() || !IsEditorUIEnabled()) {
                HIKARI::EDITOR::ClearGameViewportInputRect();
            }
#endif
            HIKARI::POST::PostSystem::BeginSceneCapture();

            DX::DxRenderer::BeginFrame();
            HIKARI::HINPUT::SetExternalMouseWheelDelta(gWindow.ConsumeMouseWheelDelta());
            HIKARI::HINPUT::Update(frame.unscaledDt);
            HIKARI::VFX::BeginFrame(frame.gameDt);
            if (gEnableImGui && gImGuiInitialized) {
#if defined(HIKARI_ENABLE_IMGUI)
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
            return true;
        }

        inline bool EndFrame() {
            if (!gGpuFrameReady) {
                return false;
            }
            GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.EndFrame");
            HIKARI::VFX::EndFrame();
            if (gEnableImGui && gImGuiInitialized && gImGuiFrameBegun) {
#if defined(HIKARI_ENABLE_IMGUI)
                ImGui::Render();
                gImGuiFrameBegun = false;
#endif
            }
            {
                GFX::PIX::ScopedGpuEvent pixScene(gCtx.cmdList, GFX::PIX::kColorRender, "Scene Layers");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuScene(
                    gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::SceneLayers);
                HIKARI::RENDERER::RenderLayerRange(HIKARI::RENDERER::RenderLayer::Background, HIKARI::RENDERER::RenderLayer::VFX, false);
            }
            {
                GFX::PIX::ScopedGpuEvent pixPost(gCtx.cmdList, GFX::PIX::kColorPost, "PostSystem");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuPost(
                    gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::PostResolve);
                HIKARI::POST::PostSystem::EndSceneCaptureAndPresent();
            }
            {
                GFX::PIX::ScopedGpuEvent pixUi(gCtx.cmdList, GFX::PIX::kColorEditor, "UI and Debug Layers");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuUi(
                    gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::UiLayers);
                HIKARI::RENDERER::RenderLayerRange(HIKARI::RENDERER::RenderLayer::UI, HIKARI::RENDERER::RenderLayer::Debug, true);
            }

            if (gEnableImGui && gImGuiInitialized && gImGuiBackendInitialized) {
#if defined(HIKARI_ENABLE_IMGUI)
                GFX::PIX::ScopedGpuEvent pixImGui(gCtx.cmdList, GFX::PIX::kColorEditor, "ImGui");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuImGui(
                    gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::ImGui);
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

            gCore.SetVSyncEnabled(RENDER3D::GetRenderQualitySettings().vSync);
            if (!gCore.EndFrame()) {
                gGpuFrameReady = false;
                HIKARI_LOG_ERROR("D3D12 EndFrame failed; stopping GPU frame loop.");
                return false;
            }
            gGpuFrameReady = false;
            GFX::PIX::Update();
            return true;
        }
    } // namespace SERVICES
} // namespace HIKARI
