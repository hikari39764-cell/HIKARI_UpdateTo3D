#pragma once
#include <vector>
#include <stack> 
#include <string>
#include <memory>
#include <DirectXMath.h>
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Transition/HIKARI_TransitionProfile.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {
    namespace POST {

        class PostEffect;

        class PostSystem
        {
        public:
            struct BloomDebugStats {
                bool enabled = false;
                bool initialized = false;
                bool failed = false;
                uint32_t passCount = 0;
                int textureWidth = 0;
                int textureHeight = 0;
                float threshold = 0.0f;
                float intensity = 0.0f;
                float radius = 0.0f;
                uint32_t downsampleCount = 0;
            };

            struct FxaaSettings {
                float edgeThreshold = 0.125f;
                float edgeThresholdMin = 0.0312f;
                float subpixelQuality = 0.75f;
            };

            static void Initialize(const GFX::Context& ctx);
            static void UpdateContext(const GFX::Context& ctx);
            static void Shutdown();

            static void UpdateCommonParams(float deltaTime);
            static void SetIntensity(float intensity);
            static void SetCombo(float combo);

            static void ClearEffects();
            static void AddEffect(PostEffect* effect);
            static bool SetGlobalProfile(const std::string& profileId, const DirectX::XMFLOAT4(&paramValues)[16]);
            static void ClearGlobalProfile();
            static void SetBloomSettings(const BloomSettings& settings);
            static void SetToneMappingSettings(const ToneMappingSettings& settings);
            static void SetFxaaSettings(const FxaaSettings& settings);
            static const FxaaSettings& GetFxaaSettings();
            static const BloomDebugStats& GetBloomDebugStats();
            static std::string DumpFrameState();
            static void LogFrameState(const char* reason);
            static void RequestFrameDump();
            static void SetTransitionState(const TransitionVisualState& state);
            static void ClearTransitionState();

            // --- ?景捕? ---
            static void BeginSceneCapture();
            static bool HasCurrentRenderTarget();
            static D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetDepthSrv();
            static bool RebindCurrentRenderTarget();
            static D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetDsv();
            static D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetReadOnlyDsv();
            static bool BeginCurrentRenderTargetDepthRead();
            static void EndCurrentRenderTargetDepthRead();
            static void EndSceneCaptureAndPresent(); // ?里会自??用光照合成
            static bool EndSceneCaptureToEditorViewport();
            static bool IsSceneCaptureActive();
            static void SetSceneCaptureSize(int width, int height);
            static void GetSceneCaptureSize(int& outWidth, int& outHeight);
            static bool IsEditorViewportReady();
            static D3D12_GPU_DESCRIPTOR_HANDLE GetEditorViewportSrv();
            static int GetEditorViewportWidth();
            static int GetEditorViewportHeight();

            // Captures current sceneRT_ color into a stable snapshot texture
            // so later depth-aware / transparent / distortion materials can sample it safely.
            static bool CaptureSceneColorSnapshot();
            static bool IsSceneColorReady();
            static D3D12_GPU_DESCRIPTOR_HANDLE GetSceneColorSrv();
            // Returns the current SceneColor snapshot size.
            // May be 0 before the first successful CaptureSceneColorSnapshot().
            static int GetSceneColorWidth();
            static int GetSceneColorHeight();

            // --- 光照系?  ---
            // ?置?境光?色 (R,G,B), 0.0=全黑, 1.0=全亮
            static void SetAmbientColor(float r, float g, float b);

            // ?始?制光照?? (在?之后?制 Sprite 光源)
            static void BeginLightCapture();

            // ?束?制光照??
            static void EndLightCapture();


            // --- ??系? ---
            static void BeginLayer(PostChain& chain, float r = 0, float g = 0, float b = 0, float a = 0);
            static void EndLayer(BlendOption blendMode = BlendOption::Alpha);

        private:
            static void EnsureSceneRTSize();
            static void EnsureEditorViewportRTSize(int width, int height);
            static bool EnsureToneMappedLdrRTSize(int width, int height, DXGI_FORMAT format);
            static void RefreshEditorViewportSrvDescriptor();
            static void EnsureSceneColorSnapshotRTSize();
            static void RefreshSceneColorSrvDescriptor();
            static bool IsEditorViewportTextureCurrent();
            static RenderTarget2D* EndSceneCaptureAndResolveFinal();
            static RenderTarget2D* ResolveFinalSceneToLdr(RenderTarget2D& finalSceneRT, DXGI_FORMAT outputFormat);
            static bool DrawResolvedSceneToCurrentTarget(RenderTarget2D& resolvedSceneRT, DXGI_FORMAT outputFormat);
            static void BindBackBufferFullViewport();
            static RenderTarget2D* ApplyBloom(RenderTarget2D& source);
            static RenderTarget2D* ApplyFxaa(RenderTarget2D& source);
            static bool EnsureBloomEffects(uint32_t blurPairCount);
            static bool EnsureToneMappingEffect();
            static bool EnsureFxaaEffect();

        private:
            static bool initialized_;
            static GFX::Context context_;

            static RenderTarget2D sceneRT_;
            static RenderTarget2D editorViewportRT_;
            static RenderTarget2D sceneColorSnapshotRT_;
            static RenderTarget2D toneMappedLdrRT_;
            static bool sceneColorReady_;
            static D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSrvCpu_;
            static D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvGpu_;

            // [新增] ??用于画光的 RT
            static RenderTarget2D lightRT_;
            static float ambientColor_[3];
            static float lightRTClearColor_[3];
            static bool useLighting_;

            static PostChain globalChain_;
            static PostChain bloomChain_;
            static PostChain fxaaChain_;
            static QuadDrawer quad_;
            static CommonParams commonParams_;
            static float elapsedTime_;
            static std::string activeGlobalProfileId_;
            static PostProfile activeGlobalProfile_;
            static std::vector<std::unique_ptr<PostEffect>> activeGlobalEffects_;
            static std::vector<std::unique_ptr<PostEffect>> activeBloomEffects_;
            static BloomSettings bloomSettings_;
            static ToneMappingSettings toneMappingSettings_;
            static FxaaSettings fxaaSettings_;
            static BloomDebugStats bloomDebugStats_;
            static uint32_t activeBloomBlurPairCount_;
            static std::unique_ptr<PostEffect> toneMappingEffect_;
            static CommonParams toneMappingParams_;
            static std::unique_ptr<PostEffect> fxaaEffect_;
            static CommonParams fxaaParams_;
            static bool dumpNextFrame_;
            static bool transitionActive_;
            static std::string activeTransitionProfileId_;
            static TransitionProfile activeTransitionProfile_;
            static std::unique_ptr<PostEffect> transitionEffect_;
            static CommonParams transitionParams_;
            static bool sceneCaptureActive_;
            static bool editorViewportReady_;
            static int requestedSceneCaptureWidth_;
            static int requestedSceneCaptureHeight_;
            static D3D12_CPU_DESCRIPTOR_HANDLE editorViewportSrvCpu_;
            static D3D12_GPU_DESCRIPTOR_HANDLE editorViewportSrvGpu_;

            struct LayerInfo {
                RenderTarget2D* rt;
                PostChain* chain;
            };
            static std::stack<LayerInfo> rtStack_;
        };

    } // POST
} // HIKARI
