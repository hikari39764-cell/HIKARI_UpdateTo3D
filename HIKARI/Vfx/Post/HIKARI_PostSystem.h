#pragma once
#include <vector>
#include <stack> 
#include <string>
#include <memory>
#include <DirectXMath.h>
#include "HIKARI_RenderTarget2D.h"
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
            static const BloomDebugStats& GetBloomDebugStats();
            static std::string DumpFrameState();
            static void LogFrameState(const char* reason);
            static void RequestFrameDump();
            static void SetTransitionState(const TransitionVisualState& state);
            static void ClearTransitionState();

            // --- 场景捕获 ---
            static void BeginSceneCapture();
            static bool RebindCurrentRenderTarget();
            static void EndSceneCaptureAndPresent(); // 这里会自动应用光照合成

            // --- 光照系统  ---
            // 设置环境光颜色 (R,G,B), 0.0=全黑, 1.0=全亮
            static void SetAmbientColor(float r, float g, float b);

            // 开始绘制光照贴图 (在这之后绘制 Sprite 光源)
            static void BeginLightCapture();

            // 结束绘制光照贴图
            static void EndLightCapture();


            // --- 图层系统 ---
            static void BeginLayer(PostChain& chain, float r = 0, float g = 0, float b = 0, float a = 0);
            static void EndLayer(BlendOption blendMode = BlendOption::Alpha);

        private:
            static void EnsureSceneRTSize();
            static RenderTarget2D* ApplyBloom(RenderTarget2D& source);
            static bool EnsureBloomEffects(uint32_t blurPairCount);
            static bool EnsureToneMappingEffect();

        private:
            static bool initialized_;
            static GFX::Context context_;

            static RenderTarget2D sceneRT_;

            // [新增] 专门用于画光的 RT
            static RenderTarget2D lightRT_;
            static float ambientColor_[3];
            static bool useLighting_;

            static PostChain globalChain_;
            static PostChain bloomChain_;
            static QuadDrawer quad_;
            static CommonParams commonParams_;
            static float elapsedTime_;
            static std::string activeGlobalProfileId_;
            static PostProfile activeGlobalProfile_;
            static std::vector<std::unique_ptr<PostEffect>> activeGlobalEffects_;
            static std::vector<std::unique_ptr<PostEffect>> activeBloomEffects_;
            static BloomSettings bloomSettings_;
            static ToneMappingSettings toneMappingSettings_;
            static BloomDebugStats bloomDebugStats_;
            static uint32_t activeBloomBlurPairCount_;
            static std::unique_ptr<PostEffect> toneMappingEffect_;
            static CommonParams toneMappingParams_;
            static bool dumpNextFrame_;
            static bool transitionActive_;
            static std::string activeTransitionProfileId_;
            static TransitionProfile activeTransitionProfile_;
            static std::unique_ptr<PostEffect> transitionEffect_;
            static CommonParams transitionParams_;

            struct LayerInfo {
                RenderTarget2D* rt;
                PostChain* chain;
            };
            static std::stack<LayerInfo> rtStack_;
        };

    } // POST
} // HIKARI
