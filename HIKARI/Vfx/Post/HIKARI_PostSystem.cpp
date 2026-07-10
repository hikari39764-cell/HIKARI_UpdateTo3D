#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Core/HIKARI_Utility.h"
#include <algorithm>
#include <cassert>
#include <sstream>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_Core.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Temporal/HIKARI_TaaResolvePass.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

namespace HIKARI {
    namespace POST {

        namespace {
            constexpr int kMinEditorViewportSize = 16;
            constexpr int kMaxEditorViewportSize = 8192;

            struct LetterboxRect {
                float x;
                float y;
                float width;
                float height;
            };

            static LetterboxRect ComputeLetterboxRect(int backBufferW, int backBufferH)
            {
                if (backBufferW <= 0 || backBufferH <= 0) {
                    return { 0.0f, 0.0f, 1.0f, 1.0f };
                }

                const float targetAspect = static_cast<float>(kScreenW) / static_cast<float>(kScreenH);
                const float backBufferAspect = static_cast<float>(backBufferW) / static_cast<float>(backBufferH);

                int vpW = backBufferW;
                int vpH = backBufferH;
                int vpX = 0;
                int vpY = 0;

                if (backBufferAspect > targetAspect) {
                    vpW = static_cast<int>(static_cast<float>(backBufferH) * targetAspect + 0.5f);
                    vpX = (backBufferW - vpW) / 2;
                }
                else {
                    vpH = static_cast<int>(static_cast<float>(backBufferW) / targetAspect + 0.5f);
                    vpY = (backBufferH - vpH) / 2;
                }

                return {
                    static_cast<float>(vpX),
                    static_cast<float>(vpY),
                    static_cast<float>(vpW),
                    static_cast<float>(vpH)
                };
            }
        }

        bool PostSystem::initialized_ = false;
        GFX::Context PostSystem::context_{};
        RenderTarget2D PostSystem::sceneRT_{};
        RenderTarget2D PostSystem::editorViewportRT_{};
        RenderTarget2D PostSystem::sceneColorSnapshotRT_{};
        RenderTarget2D PostSystem::toneMappedLdrRT_{};
        bool PostSystem::sceneColorReady_ = false;
        D3D12_CPU_DESCRIPTOR_HANDLE PostSystem::sceneColorSrvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE PostSystem::sceneColorSrvGpu_{};
        RenderTarget2D PostSystem::lightRT_{};
        QuadDrawer PostSystem::quad_{};
        PostChain PostSystem::globalChain_{};
        PostChain PostSystem::bloomChain_{};
        PostChain PostSystem::fxaaChain_{};
        CommonParams PostSystem::commonParams_{};
        float PostSystem::elapsedTime_ = 0.0f;
        std::stack<PostSystem::LayerInfo> PostSystem::rtStack_{};
        float PostSystem::ambientColor_[3] = { 1.0f, 1.0f, 1.0f };
        float PostSystem::lightRTClearColor_[3] = { 1.0f, 1.0f, 1.0f };
        bool PostSystem::useLighting_ = false;
        std::string PostSystem::activeGlobalProfileId_{};
        PostProfile PostSystem::activeGlobalProfile_{};
        std::vector<std::unique_ptr<PostEffect>> PostSystem::activeGlobalEffects_{};
        std::vector<std::unique_ptr<PostEffect>> PostSystem::activeBloomEffects_{};
        BloomSettings PostSystem::bloomSettings_{};
        ToneMappingSettings PostSystem::toneMappingSettings_{};
        PostSystem::FxaaSettings PostSystem::fxaaSettings_{};
        PostSystem::BloomDebugStats PostSystem::bloomDebugStats_{};
        uint32_t PostSystem::activeBloomBlurPairCount_ = 0;
        std::unique_ptr<PostEffect> PostSystem::toneMappingEffect_{};
        CommonParams PostSystem::toneMappingParams_{};
        std::unique_ptr<PostEffect> PostSystem::fxaaEffect_{};
        CommonParams PostSystem::fxaaParams_{};
        bool PostSystem::dumpNextFrame_ = false;
        bool PostSystem::transitionActive_ = false;
        std::string PostSystem::activeTransitionProfileId_{};
        TransitionProfile PostSystem::activeTransitionProfile_{};
        std::unique_ptr<PostEffect> PostSystem::transitionEffect_{};
        CommonParams PostSystem::transitionParams_{};
        bool PostSystem::sceneCaptureActive_ = false;
        bool PostSystem::editorViewportReady_ = false;
        int PostSystem::requestedSceneCaptureWidth_ = 0;
        int PostSystem::requestedSceneCaptureHeight_ = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE PostSystem::editorViewportSrvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE PostSystem::editorViewportSrvGpu_{};


        void PostSystem::Initialize(const GFX::Context& ctx)
        {
            context_ = ctx;
            PostEffect::UpdateContext(ctx);
            globalChain_.UpdateContext(ctx);
            bloomChain_.UpdateContext(ctx);
            fxaaChain_.UpdateContext(ctx);
            globalChain_.SetDebugName("PostSystem.GlobalChain");
            bloomChain_.SetDebugName("PostSystem.BloomChain");
            fxaaChain_.SetDebugName("PostSystem.FXAAChain");
            if (initialized_) return;

            if (context_.device == nullptr || context_.cmdList == nullptr) {
                DEBUGLOG::PushRenderError("[PostSystem][ERROR] Initialize received invalid GFX context.");
                GFX::DumpD3D12InfoQueue(context_.device, "PostSystem Initialize invalid GFX context");
                initialized_ = false;
                return;
            }

            if (!quad_.Init(context_)) {
                DEBUGLOG::PushRenderError("[PostSystem][ERROR] QuadDrawer initialization failed.");
                GFX::DumpD3D12InfoQueue(context_.device, "PostSystem QuadDrawer initialization failed");
                initialized_ = false;
                return;
            }

            initialized_ = true;
        }

        void PostSystem::UpdateContext(const GFX::Context& ctx)
        {
            context_ = ctx;
            PostEffect::UpdateContext(ctx);
            globalChain_.UpdateContext(ctx);
            bloomChain_.UpdateContext(ctx);
            fxaaChain_.UpdateContext(ctx);
            quad_.UpdateContext(ctx);
            sceneRT_.UpdateContext(ctx);
            editorViewportRT_.UpdateContext(ctx);
            sceneColorSnapshotRT_.UpdateContext(ctx);
            toneMappedLdrRT_.UpdateContext(ctx);
            lightRT_.UpdateContext(ctx);
        }

        void PostSystem::Shutdown()
        {
            if (!initialized_) return;
            ClearGlobalProfile();
            ClearTransitionState();
            globalChain_.Finalize();
            bloomChain_.Finalize();
            fxaaChain_.Finalize();
            activeBloomEffects_.clear();
            toneMappingEffect_.reset();
            fxaaEffect_.reset();
            sceneRT_.Finalize();
            editorViewportRT_.Finalize();
            sceneColorSnapshotRT_.Finalize();
            toneMappedLdrRT_.Finalize();
            lightRT_.Finalize();
            quad_.Finalize();

            while (!rtStack_.empty()) rtStack_.pop();
            sceneCaptureActive_ = false;
            editorViewportReady_ = false;
            editorViewportSrvCpu_ = {};
            editorViewportSrvGpu_ = {};
            sceneColorReady_ = false;
            sceneColorSrvCpu_ = {};
            sceneColorSrvGpu_ = {};
            initialized_ = false;
        }

        void PostSystem::UpdateCommonParams(float deltaTime)
        {
            if (!initialized_) { Initialize(context_); }

            int captureW = 0;
            int captureH = 0;
            GetSceneCaptureSize(captureW, captureH);
            commonParams_.resolutionX = static_cast<float>(captureW);
            commonParams_.resolutionY = static_cast<float>(captureH);

            commonParams_.deltaTime = deltaTime;
            elapsedTime_ += deltaTime;
            commonParams_.time = elapsedTime_;
        }

        void PostSystem::SetIntensity(float intensity)
        {
            commonParams_.intensity = intensity;
        }
        void PostSystem::SetCombo(float combo)
        {
            commonParams_.combo = combo;
        }
        void PostSystem::ClearEffects()
        {
            globalChain_.Clear();
        }
        void PostSystem::AddEffect(PostEffect* effect)
        {
            globalChain_.Add(effect);
        }

        bool PostSystem::SetGlobalProfile(const std::string& profileId, const DirectX::XMFLOAT4(&paramValues)[16]) {
            if (profileId.empty()) {
                ClearGlobalProfile();
                return false;
            }

            bool needsRebuild = (activeGlobalProfileId_ != profileId || activeGlobalEffects_.empty());
            if (needsRebuild) {
                PostProfile loadedProfile{};
                if (!PostProfile::LoadById(profileId, loadedProfile)) {
                    ClearGlobalProfile();
                    return false;
                }

                globalChain_.Clear();
                activeGlobalEffects_.clear();
                activeGlobalEffects_.reserve(loadedProfile.passes.size());
                for (const auto& pass : loadedProfile.passes) {
                    auto effect = std::make_unique<PostEffect>();
                    std::wstring shaderPath = L"HIKARI/Shaders/";
                    shaderPath += std::wstring(pass.shaderId.begin(), pass.shaderId.end());
                    shaderPath += L".hlsl";
                    if (!effect->LoadPixelShader(shaderPath.c_str())) {
                        continue;
                    }
                    globalChain_.Add(effect.get());
                    activeGlobalEffects_.push_back(std::move(effect));
                }
                activeGlobalProfile_ = std::move(loadedProfile);
                activeGlobalProfileId_ = profileId;
            }

            activeGlobalProfile_.ResetValuesFromDefaults();
            for (size_t i = 0; i < activeGlobalProfile_.values.size(); ++i) {
                activeGlobalProfile_.values[i] = paramValues[i];
            }
            activeGlobalProfile_.ApplyToCommonParams(commonParams_);
            return globalChain_.HasAny();
        }

        void PostSystem::ClearGlobalProfile() {
            activeGlobalProfileId_.clear();
            activeGlobalProfile_ = PostProfile{};
            globalChain_.Clear();
            activeGlobalEffects_.clear();
        }

        void PostSystem::SetBloomSettings(const BloomSettings& settings) {
            bloomSettings_ = settings;
        }

        void PostSystem::SetToneMappingSettings(const ToneMappingSettings& settings) {
            toneMappingSettings_ = settings;
        }

        void PostSystem::SetFxaaSettings(const FxaaSettings& settings) {
            fxaaSettings_ = settings;
            fxaaSettings_.edgeThreshold = std::clamp(fxaaSettings_.edgeThreshold, 0.0312f, 0.333f);
            fxaaSettings_.edgeThresholdMin = std::clamp(fxaaSettings_.edgeThresholdMin, 0.0f, 0.0833f);
            fxaaSettings_.subpixelQuality = std::clamp(fxaaSettings_.subpixelQuality, 0.0f, 1.0f);
        }

        const PostSystem::FxaaSettings& PostSystem::GetFxaaSettings() {
            return fxaaSettings_;
        }

        const PostSystem::BloomDebugStats& PostSystem::GetBloomDebugStats() {
            return bloomDebugStats_;
        }

        std::string PostSystem::DumpFrameState() {
            const RENDER3D::RenderQualitySettings& renderQuality =
                RENDER3D::GetRenderQualitySettings();
            std::ostringstream oss;
            oss << "[PostSystem] initialized=" << initialized_
                << " globalChainHasAny=" << globalChain_.HasAny()
                << " bloomEnabled=" << bloomSettings_.enabled
                << " bloomPassCount=" << bloomDebugStats_.passCount
                << " aaMode=" << RENDER3D::RenderAntiAliasingModeLabel(
                    renderQuality.antiAliasingMode)
                << " toneMappingEnabled=" << toneMappingSettings_.enabled
                << " toneMappingMode=" << toneMappingSettings_.mode
                << " transitionActive=" << transitionActive_
                << " useLighting=" << useLighting_
                << "\n  " << sceneRT_.DumpState()
                << "\n  " << toneMappedLdrRT_.DumpState()
                << "\n  " << lightRT_.DumpState()
                << "\n  " << globalChain_.DumpState()
                << "\n  " << bloomChain_.DumpState()
                << "\n  " << fxaaChain_.DumpState()
                << "\n  " << quad_.DumpState();
            return oss.str();
        }

        void PostSystem::LogFrameState(const char* reason) {
            std::ostringstream oss;
            oss << "[PostSystem][DUMP] reason=" << (reason ? reason : "") << "\n"
                << DumpFrameState();
            DEBUGLOG::PushRenderError(oss.str());
        }

        void PostSystem::RequestFrameDump() {
            dumpNextFrame_ = true;
        }

        bool PostSystem::EnsureToneMappingEffect() {
            if (toneMappingEffect_) {
                return true;
            }

            auto effect = std::make_unique<PostEffect>();
            if (!effect->LoadPixelShader(L"HIKARI/Shaders/Post_ToneMappingPS.hlsl")) {
                DEBUGLOG::PushRenderError("[PostSystem][ToneMapping][ERROR] LoadPixelShader failed. shader=HIKARI/Shaders/Post_ToneMappingPS.hlsl");
                LogFrameState("ToneMapping effect load failed");
                GFX::DumpD3D12InfoQueue(context_.device, "ToneMapping effect load failed");
                return false;
            }
            toneMappingEffect_ = std::move(effect);
            return true;
        }

        bool PostSystem::EnsureFxaaEffect() {
            if (fxaaEffect_ && fxaaChain_.HasAny()) {
                return true;
            }

            if (!fxaaEffect_) {
                auto effect = std::make_unique<PostEffect>();
                if (!effect->LoadPixelShader(L"HIKARI/Shaders/Post_FXAA.hlsl")) {
                    DEBUGLOG::PushRenderError("[PostSystem][FXAA][ERROR] LoadPixelShader failed. shader=HIKARI/Shaders/Post_FXAA.hlsl");
                    LogFrameState("FXAA effect load failed");
                    GFX::DumpD3D12InfoQueue(context_.device, "FXAA effect load failed");
                    return false;
                }
                fxaaEffect_ = std::move(effect);
            }

            fxaaChain_.Clear();
            fxaaChain_.Add(fxaaEffect_.get());
            return true;
        }

        bool PostSystem::EnsureBloomEffects(uint32_t blurPairCount) {
            blurPairCount = std::clamp<uint32_t>(blurPairCount, 1u, 5u);
            if (activeBloomBlurPairCount_ == blurPairCount && !activeBloomEffects_.empty() && bloomChain_.HasAny()) {
                return true;
            }

            bloomChain_.Clear();
            activeBloomEffects_.clear();
            activeBloomBlurPairCount_ = 0;

            auto addEffect = [](const wchar_t* shaderPath) -> std::unique_ptr<PostEffect> {
                auto effect = std::make_unique<PostEffect>();
                if (!effect->LoadPixelShader(shaderPath)) {
                    return nullptr;
                }
                return effect;
            };

            if (auto extract = addEffect(L"HIKARI/Shaders/Post_BloomExtractPS.hlsl")) {
                bloomChain_.Add(extract.get());
                activeBloomEffects_.push_back(std::move(extract));
            } else {
                LogFrameState("EnsureBloomEffects Extract failed");
                return false;
            }

            for (uint32_t i = 0; i < blurPairCount; ++i) {
                auto blurH = addEffect(L"HIKARI/Shaders/Post_BloomBlurHPS.hlsl");
                auto blurV = addEffect(L"HIKARI/Shaders/Post_BloomBlurVPS.hlsl");
                if (!blurH || !blurV) {
                    bloomChain_.Clear();
                    activeBloomEffects_.clear();
                    LogFrameState("EnsureBloomEffects Blur failed");
                    return false;
                }
                bloomChain_.Add(blurH.get());
                activeBloomEffects_.push_back(std::move(blurH));
                bloomChain_.Add(blurV.get());
                activeBloomEffects_.push_back(std::move(blurV));
            }

            activeBloomBlurPairCount_ = blurPairCount;
            return true;
        }

        RenderTarget2D* PostSystem::ApplyBloom(RenderTarget2D& source) {
            bloomDebugStats_ = {};
            bloomDebugStats_.enabled = bloomSettings_.enabled;
            bloomDebugStats_.threshold = bloomSettings_.threshold;
            bloomDebugStats_.intensity = bloomSettings_.intensity;
            bloomDebugStats_.radius = bloomSettings_.radius;
            bloomDebugStats_.downsampleCount = std::clamp<uint32_t>(bloomSettings_.downsampleCount, 1u, 5u);
            bloomDebugStats_.textureWidth = source.GetWidth();
            bloomDebugStats_.textureHeight = source.GetHeight();

            if (!bloomSettings_.enabled || bloomSettings_.intensity <= 0.0f) {
                return nullptr;
            }

            if (!EnsureBloomEffects(bloomDebugStats_.downsampleCount)) {
                bloomDebugStats_.failed = true;
                LogFrameState("ApplyBloom EnsureBloomEffects failed");
                return nullptr;
            }

            CommonParams bloomParams = commonParams_;
            bloomParams.user[0] = {
                (std::max)(0.0f, bloomSettings_.threshold),
                (std::max)(0.0f, bloomSettings_.intensity),
                (std::max)(0.0f, bloomSettings_.radius),
                static_cast<float>(bloomDebugStats_.downsampleCount)
            };
            bloomParams.user[1] = {
                source.GetWidth() > 0 ? 1.0f / static_cast<float>(source.GetWidth()) : 1.0f,
                source.GetHeight() > 0 ? 1.0f / static_cast<float>(source.GetHeight()) : 1.0f,
                0.0f,
                0.0f
            };

            bloomDebugStats_.initialized = true;
            bloomDebugStats_.passCount = 1u + bloomDebugStats_.downsampleCount * 2u;
            return bloomChain_.Execute(source, quad_, bloomParams);
        }

        RenderTarget2D* PostSystem::ApplyFxaa(RenderTarget2D& source) {
            if (source.GetResource() == nullptr) {
                return nullptr;
            }

            if (!EnsureFxaaEffect()) {
                return nullptr;
            }

            const float width = static_cast<float>((std::max)(source.GetWidth(), 1));
            const float height = static_cast<float>((std::max)(source.GetHeight(), 1));
            fxaaParams_ = commonParams_;
            fxaaParams_.resolutionX = width;
            fxaaParams_.resolutionY = height;
            fxaaParams_.user[0] = {
                width,
                height,
                1.0f / width,
                1.0f / height
            };
            fxaaParams_.user[1] = {
                1.0f,
                fxaaSettings_.edgeThreshold,
                fxaaSettings_.edgeThresholdMin,
                fxaaSettings_.subpixelQuality
            };

            return fxaaChain_.Execute(source, quad_, fxaaParams_);
        }

        void PostSystem::SetTransitionState(const TransitionVisualState& state) {
            if (!state.active) {
                ClearTransitionState();
                return;
            }

            std::string requestedProfile = state.profileId;
            if (requestedProfile.empty()) {
                requestedProfile = "noise_wipe";
            }

            if (activeTransitionProfileId_ != requestedProfile || !transitionEffect_) {
                TransitionProfile loadedProfile{};
                if (!TransitionProfile::LoadById(requestedProfile, loadedProfile)) {
                    requestedProfile = "noise_wipe";
                    if (!TransitionProfile::LoadById(requestedProfile, loadedProfile)) {
                        ClearTransitionState();
                        return;
                    }
                }

                std::unique_ptr<PostEffect> effect = std::make_unique<PostEffect>();
                std::wstring shaderPath = L"HIKARI/Shaders/";
                shaderPath += std::wstring(loadedProfile.shaderId.begin(), loadedProfile.shaderId.end());
                shaderPath += L".hlsl";
                if (!effect->LoadPixelShader(shaderPath.c_str())) {
                    ClearTransitionState();
                    return;
                }

                activeTransitionProfile_ = std::move(loadedProfile);
                activeTransitionProfileId_ = requestedProfile;
                transitionEffect_ = std::move(effect);
            }

            transitionActive_ = true;
            transitionParams_ = commonParams_;
            activeTransitionProfile_.ApplyToCommonParams(transitionParams_);
            transitionParams_.user[14] = {
                (std::min)(1.0f, (std::max)(0.0f, state.progress)),
                state.isTransitionIn ? 1.0f : 0.0f,
                0.0f,
                0.0f
            };
        }

        void PostSystem::ClearTransitionState() {
            transitionActive_ = false;
            activeTransitionProfileId_.clear();
            activeTransitionProfile_ = TransitionProfile{};
            transitionEffect_.reset();
            transitionParams_ = CommonParams{};
        }

        void PostSystem::SetAmbientColor(float r, float g, float b)
        {
            ambientColor_[0] = r;
            ambientColor_[1] = g;
            ambientColor_[2] = b;
        }

        bool PostSystem::IsSceneCaptureActive()
        {
            return sceneCaptureActive_;
        }

        void PostSystem::SetSceneCaptureSize(int width, int height)
        {
            if (width <= 0 || height <= 0) {
                const bool changed =
                    requestedSceneCaptureWidth_ != 0 ||
                    requestedSceneCaptureHeight_ != 0;
                requestedSceneCaptureWidth_ = 0;
                requestedSceneCaptureHeight_ = 0;
                if (changed) {
                    editorViewportReady_ = false;
                    sceneColorReady_ = false;
                }
                return;
            }

            const int clampedWidth = std::clamp(width, kMinEditorViewportSize, kMaxEditorViewportSize);
            const int clampedHeight = std::clamp(height, kMinEditorViewportSize, kMaxEditorViewportSize);
            if (requestedSceneCaptureWidth_ == clampedWidth &&
                requestedSceneCaptureHeight_ == clampedHeight) {
                return;
            }

            requestedSceneCaptureWidth_ = clampedWidth;
            requestedSceneCaptureHeight_ = clampedHeight;
            // GameView のサイズ変更中は古い SRV を表示・参照しない。
            editorViewportReady_ = false;
            sceneColorReady_ = false;
        }

        void PostSystem::GetSceneCaptureSize(int& outWidth, int& outHeight)
        {
            outWidth = (requestedSceneCaptureWidth_ > 0) ? requestedSceneCaptureWidth_ : kScreenW;
            outHeight = (requestedSceneCaptureHeight_ > 0) ? requestedSceneCaptureHeight_ : kScreenH;
        }

        bool PostSystem::IsEditorViewportReady()
        {
            return editorViewportReady_ &&
                editorViewportSrvGpu_.ptr != 0 &&
                editorViewportRT_.GetResource() != nullptr &&
                IsEditorViewportTextureCurrent();
        }

        D3D12_GPU_DESCRIPTOR_HANDLE PostSystem::GetEditorViewportSrv()
        {
            return editorViewportSrvGpu_;
        }

        int PostSystem::GetEditorViewportWidth()
        {
            return editorViewportRT_.GetWidth();
        }

        int PostSystem::GetEditorViewportHeight()
        {
            return editorViewportRT_.GetHeight();
        }

        bool PostSystem::IsSceneColorReady()
        {
            return sceneColorReady_ &&
                sceneColorSnapshotRT_.GetResource() != nullptr &&
                sceneColorSrvGpu_.ptr != 0;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE PostSystem::GetSceneColorSrv()
        {
            if (!IsSceneColorReady()) {
                return {};
            }

            return sceneColorSrvGpu_;
        }

        int PostSystem::GetSceneColorWidth()
        {
            return sceneColorSnapshotRT_.GetWidth();
        }

        int PostSystem::GetSceneColorHeight()
        {
            return sceneColorSnapshotRT_.GetHeight();
        }

        void PostSystem::EnsureSceneRTSize()
        {
            int w = 0;
            int h = 0;
            GetSceneCaptureSize(w, h);
            if (w <= 0 || h <= 0) return;

            sceneRT_.UpdateContext(context_);
            lightRT_.UpdateContext(context_);

            const bool sceneInvalid =
                (!sceneRT_.GetResource() ||
                    sceneRT_.GetWidth() != w ||
                    sceneRT_.GetHeight() != h ||
                    sceneRT_.GetFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT ||
                    !sceneRT_.HasDepth());

            if (sceneInvalid) {
                sceneRT_.Finalize();
                sceneRT_.SetDebugName("Post.SceneRT.HDR");
               /* HIKARI_LOG_INFO("[PostSystem][HDR] Recreate SceneRT size=" + std::to_string(w) + "x" + std::to_string(h) + " format=R16G16B16A16_FLOAT withDepth=true");*/
                const bool ok = sceneRT_.Init(
                    w, h,
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    true,
                    { 0.0f, 0.0f, 0.0f, 1.0f }   // sceneRT 常用黑底不透明
                );
                if (!ok) {
                    DEBUGLOG::PushRenderError("[PostSystem][HDR][ERROR] SceneRT creation failed");
                    LogFrameState("SceneRT HDR creation failed");
                    GFX::DumpD3D12InfoQueue(context_.device, "SceneRT HDR creation failed");
                }
            }

            const bool lightClearChanged =
                lightRTClearColor_[0] != ambientColor_[0] ||
                lightRTClearColor_[1] != ambientColor_[1] ||
                lightRTClearColor_[2] != ambientColor_[2];
            if (!lightRT_.GetResource() ||
                lightRT_.GetWidth() != w ||
                lightRT_.GetHeight() != h ||
                lightRT_.GetFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT ||
                lightClearChanged) {
                lightRT_.Finalize();
                lightRT_.SetDebugName("Post.LightRT.HDR");
                // ClearRTV の値と optimized clear を一致させる。
                lightRTClearColor_[0] = ambientColor_[0];
                lightRTClearColor_[1] = ambientColor_[1];
                lightRTClearColor_[2] = ambientColor_[2];
                lightRT_.Init(
                    w, h,
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    false,
                    { lightRTClearColor_[0], lightRTClearColor_[1], lightRTClearColor_[2], 1.0f }
                );
            }
        }

        void PostSystem::EnsureEditorViewportRTSize(int width, int height)
        {
            if (width <= 0 || height <= 0) {
                return;
            }

            editorViewportRT_.UpdateContext(context_);

            const bool invalid =
                !editorViewportRT_.GetResource() ||
                editorViewportRT_.GetWidth() != width ||
                editorViewportRT_.GetHeight() != height ||
                editorViewportRT_.GetFormat() != DXGI_FORMAT_R8G8B8A8_UNORM ||
                editorViewportRT_.HasDepth();

            if (!invalid) {
                return;
            }

            editorViewportRT_.Finalize();
            editorViewportRT_.SetDebugName("Post.EditorGameView.LDR");
            const bool ok = editorViewportRT_.Init(
                width,
                height,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                false,
                { 0.0f, 0.0f, 0.0f, 1.0f });

            if (!ok) {
                editorViewportReady_ = false;
                DEBUGLOG::PushRenderError("[PostSystem][EditorViewport][ERROR] Editor viewport RT creation failed");
                GFX::DumpD3D12InfoQueue(context_.device, "Editor viewport RT creation failed");
                return;
            }

            RefreshEditorViewportSrvDescriptor();
        }

        bool PostSystem::EnsureToneMappedLdrRTSize(int width, int height, DXGI_FORMAT format)
        {
            if (width <= 0 || height <= 0 || format == DXGI_FORMAT_UNKNOWN) {
                return false;
            }

            toneMappedLdrRT_.UpdateContext(context_);

            const bool invalid =
                !toneMappedLdrRT_.GetResource() ||
                toneMappedLdrRT_.GetWidth() != width ||
                toneMappedLdrRT_.GetHeight() != height ||
                toneMappedLdrRT_.GetFormat() != format ||
                toneMappedLdrRT_.HasDepth();

            if (!invalid) {
                return true;
            }

            toneMappedLdrRT_.Finalize();
            toneMappedLdrRT_.SetDebugName("Post.ToneMappedLDR");
            const bool ok = toneMappedLdrRT_.Init(
                width,
                height,
                format,
                false,
                { 0.0f, 0.0f, 0.0f, 1.0f });

            if (!ok) {
                DEBUGLOG::PushRenderError("[PostSystem][ToneMapping][ERROR] LDR resolve RT creation failed");
                GFX::DumpD3D12InfoQueue(context_.device, "ToneMapped LDR RT creation failed");
                return false;
            }

            return true;
        }

        void PostSystem::RefreshEditorViewportSrvDescriptor()
        {
            ID3D12Device* device = context_.device;
            ID3D12DescriptorHeap* srvHeap = context_.srvHeap;
            ID3D12Resource* resource = editorViewportRT_.GetResource();
            if (!device || !srvHeap || !resource) {
                editorViewportReady_ = false;
                editorViewportSrvCpu_ = {};
                editorViewportSrvGpu_ = {};
                return;
            }

            const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            const UINT editorViewportSrvIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::EditorViewport);
            editorViewportSrvCpu_ =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, editorViewportSrvIndex);
            editorViewportSrvGpu_ =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, editorViewportSrvIndex);

            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MostDetailedMip = 0;
            srv.Texture2D.MipLevels = 1;
            srv.Texture2D.PlaneSlice = 0;
            srv.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(resource, &srv, editorViewportSrvCpu_);
            editorViewportReady_ = true;
        }

        bool PostSystem::IsEditorViewportTextureCurrent()
        {
            if (requestedSceneCaptureWidth_ <= 0 || requestedSceneCaptureHeight_ <= 0) {
                return true;
            }

            return editorViewportRT_.GetWidth() == requestedSceneCaptureWidth_ &&
                editorViewportRT_.GetHeight() == requestedSceneCaptureHeight_;
        }

        void PostSystem::EnsureSceneColorSnapshotRTSize()
        {
            if (!sceneRT_.GetResource() || !sceneRT_.IsInitialized()) {
                sceneColorReady_ = false;
                return;
            }

            sceneColorSnapshotRT_.UpdateContext(context_);

            const int width = sceneRT_.GetWidth();
            const int height = sceneRT_.GetHeight();
            const DXGI_FORMAT format = sceneRT_.GetFormat();

            const bool invalid =
                !sceneColorSnapshotRT_.GetResource() ||
                sceneColorSnapshotRT_.GetWidth() != width ||
                sceneColorSnapshotRT_.GetHeight() != height ||
                sceneColorSnapshotRT_.GetFormat() != format ||
                sceneColorSnapshotRT_.HasDepth();

            if (!invalid) {
                return;
            }

            sceneColorSnapshotRT_.Finalize();
            sceneColorSnapshotRT_.SetDebugName("Post.SceneColorSnapshot");
            const bool ok = sceneColorSnapshotRT_.Init(
                width,
                height,
                format,
                false,
                { 0.0f, 0.0f, 0.0f, 1.0f });

            if (!ok) {
                sceneColorReady_ = false;
                DEBUGLOG::PushRenderError("[PostSystem][SceneColor][ERROR] SceneColor snapshot creation failed.");
                GFX::DumpD3D12InfoQueue(context_.device, "SceneColor snapshot creation failed");
                return;
            }

            RefreshSceneColorSrvDescriptor();
        }

        void PostSystem::RefreshSceneColorSrvDescriptor()
        {
            ID3D12Device* device = context_.device;
            ID3D12DescriptorHeap* srvHeap = context_.srvHeap;
            ID3D12Resource* resource = sceneColorSnapshotRT_.GetResource();

            if (device == nullptr || srvHeap == nullptr || resource == nullptr) {
                sceneColorReady_ = false;
                sceneColorSrvCpu_ = {};
                sceneColorSrvGpu_ = {};
                return;
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            const UINT sceneColorSrvIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::SceneColor);

            sceneColorSrvCpu_ =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, sceneColorSrvIndex);
            sceneColorSrvGpu_ =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, sceneColorSrvIndex);

            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = sceneColorSnapshotRT_.GetFormat();
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MostDetailedMip = 0;
            srv.Texture2D.MipLevels = 1;
            srv.Texture2D.PlaneSlice = 0;
            srv.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(resource, &srv, sceneColorSrvCpu_);
        }

        bool PostSystem::CaptureSceneColorSnapshot()
        {
            if (!initialized_ || !sceneCaptureActive_ || !sceneRT_.GetResource() || !sceneRT_.IsInitialized()) {
                sceneColorReady_ = false;
                return false;
            }

            ID3D12GraphicsCommandList* cmd = context_.cmdList;
            if (cmd == nullptr) {
                sceneColorReady_ = false;
                return false;
            }

            EnsureSceneColorSnapshotRTSize();

            ID3D12Resource* src = sceneRT_.GetResource();
            ID3D12Resource* dst = sceneColorSnapshotRT_.GetResource();
            if (src == nullptr || dst == nullptr) {
                sceneColorReady_ = false;
                return false;
            }

            sceneRT_.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
            sceneColorSnapshotRT_.TransitionColor(D3D12_RESOURCE_STATE_COPY_DEST);

            GFX::PIX::ScopedGpuEvent pixCopy(
                cmd,
                GFX::PIX::kColorPost,
                "Post.SceneColorSnapshot.Copy");
            cmd->CopyResource(dst, src);

            sceneColorSnapshotRT_.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            sceneRT_.TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

            sceneColorReady_ = true;
            return true;
        }

        void PostSystem::BeginSceneCapture()
        {
            if (!initialized_) Initialize(context_);
            if (!initialized_) {
                LogFrameState("BeginSceneCapture initialization failed");
                sceneCaptureActive_ = false;
                return;
            }

            editorViewportReady_ = false;
            sceneColorReady_ = false;
            EnsureSceneRTSize();
            if (!sceneRT_.GetResource() || !sceneRT_.IsInitialized()) {
                LogFrameState("BeginSceneCapture sceneRT invalid after EnsureSceneRTSize");
                sceneCaptureActive_ = false;
                return;
            }
            UpdateCommonParams(0.0f);


            useLighting_ = false;

            while (!rtStack_.empty()) rtStack_.pop();


            rtStack_.push({ &sceneRT_, nullptr });

            sceneRT_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
            sceneCaptureActive_ = true;
        }

        bool PostSystem::RebindCurrentRenderTarget()
        {
            if (!initialized_ || rtStack_.empty() || rtStack_.top().rt == nullptr) {
                LogFrameState("RebindCurrentRenderTarget failed");
                return false;
            }

            rtStack_.top().rt->Rebind();
            return true;
        }

        bool PostSystem::HasCurrentRenderTarget()
        {
            return initialized_ &&
                sceneCaptureActive_ &&
                !rtStack_.empty() &&
                rtStack_.top().rt != nullptr &&
                rtStack_.top().rt->IsInitialized() &&
                rtStack_.top().rt->GetResource() != nullptr;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE PostSystem::GetCurrentRenderTargetDepthSrv()
        {
            if (!HasCurrentRenderTarget()) {
                return {};
            }

            RenderTarget2D* rt = rtStack_.top().rt;
            if (rt == nullptr || !rt->HasDepth()) {
                return {};
            }
            return rt->GetDepthSrvGpu();
        }

        D3D12_CPU_DESCRIPTOR_HANDLE PostSystem::GetCurrentRenderTargetDsv()
        {
            D3D12_CPU_DESCRIPTOR_HANDLE handle{};
            if (!initialized_ || rtStack_.empty() || rtStack_.top().rt == nullptr) {
                return handle;
            }

            return rtStack_.top().rt->GetDsvHandle();
        }

        bool PostSystem::BeginCurrentRenderTargetDepthRead()
        {
            if (!initialized_ || rtStack_.empty() || rtStack_.top().rt == nullptr) {
                return false;
            }

            return rtStack_.top().rt->BeginDepthRead();
        }

        void PostSystem::EndCurrentRenderTargetDepthRead()
        {
            if (!initialized_ || rtStack_.empty() || rtStack_.top().rt == nullptr) {
                return;
            }

            rtStack_.top().rt->EndDepthRead();
        }


        void PostSystem::BeginLightCapture()
        {
            if (!initialized_) return;

            if (!lightRT_.GetResource() || !lightRT_.IsInitialized()) {
                LogFrameState("BeginLightCapture lightRT invalid");
                return;
            }


            if (!rtStack_.empty()) {
                rtStack_.top().rt->EndCapture();
            }

            useLighting_ = true;


            lightRT_.BeginCapture(ambientColor_[0], ambientColor_[1], ambientColor_[2], 1.0f);


            rtStack_.push({ &lightRT_, nullptr });
        }


        void PostSystem::EndLightCapture()
        {
            if (!initialized_ || rtStack_.empty()) return;


            if (rtStack_.top().rt == &lightRT_) {
                lightRT_.EndCapture();
                rtStack_.pop();
            }


            if (!rtStack_.empty()) {
                rtStack_.top().rt->Rebind();
            }
        }

        RenderTarget2D* PostSystem::EndSceneCaptureAndResolveFinal()
        {
            if (!initialized_) {
                LogFrameState("EndSceneCapture not initialized");
                return nullptr;
            }
            if (rtStack_.empty()) {
                LogFrameState("EndSceneCapture empty stack");
                return nullptr;
            }

            RenderTarget2D* currentRT = rtStack_.top().rt;
            GFX::GPU_PROFILE::ScopedGpuTimer gpuPostResolve(
                context_.cmdList,
                GFX::GPU_PROFILE::Pass::PostResolve);
            currentRT->EndCapture();
            rtStack_.pop();

            RenderTarget2D* finalSceneRT = currentRT;
            const RENDER3D::RenderQualitySettings& renderQuality =
                RENDER3D::GetRenderQualitySettings();
            const bool taaRequested =
                RENDER3D::IsTemporalAntiAliasingMode(
                    renderQuality.antiAliasingMode);
            const RENDER3D::TEMPORAL::TemporalFrameState& temporalFrame =
                RENDER3D::TEMPORAL::GetCurrentTemporalFrameState();
            const bool temporalFrameCurrent =
                temporalFrame.frameIndex == TIME::GetFrameContext().frameIndex &&
                temporalFrame.camera.valid &&
                temporalFrame.renderWidth ==
                    static_cast<uint32_t>((std::max)(1, finalSceneRT->GetWidth())) &&
                temporalFrame.renderHeight ==
                    static_cast<uint32_t>((std::max)(1, finalSceneRT->GetHeight()));
            if (!taaRequested ||
                !temporalFrameCurrent ||
                !temporalFrame.temporalResolveAllowed) {
                RENDER3D::TEMPORAL::MarkTemporalAntiAliasing(
                    taaRequested,
                    false);
                RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                    RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
            } else {
                const D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv =
                    currentRT->HasDepth() ? currentRT->GetDepthSrvGpu() : D3D12_GPU_DESCRIPTOR_HANDLE{};
                const bool depthReadActive =
                    sceneDepthSrv.ptr != 0 && currentRT->BeginDepthShaderRead();
                if (depthReadActive &&
                    RENDER3D::TEMPORAL::PrepareSceneColorInput(*finalSceneRT)) {
                    const RENDER3D::TEMPORAL::TemporalInputs temporalInputs =
                        RENDER3D::TEMPORAL::BuildTemporalInputs(
                            sceneDepthSrv,
                            finalSceneRT->GetSrvGpu());
                    RENDER3D::TEMPORAL::TaaResolveSettings taaSettings{};
                    taaSettings.enabled = taaRequested;
                    taaSettings.historyWeight = renderQuality.taaHistoryWeight;
                    taaSettings.varianceClipGamma =
                        renderQuality.taaVarianceClipGamma;
                    taaSettings.depthRejection =
                        renderQuality.taaDepthRejection;
                    taaSettings.luminanceRejection =
                        renderQuality.taaLuminanceRejection;
                    taaSettings.sharpness = renderQuality.taaSharpness;
                    RenderTarget2D* taaRT =
                        RENDER3D::TEMPORAL::ExecuteTaaResolvePass(
                            temporalInputs,
                            taaSettings);
                    if (taaRT != nullptr && taaRT->GetResource() != nullptr) {
                        finalSceneRT = taaRT;
                    } else {
                        RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                            RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
                    }
                } else {
                    RENDER3D::TEMPORAL::MarkTemporalAntiAliasing(
                        taaRequested,
                        false);
                    RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                        RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
                }
                if (depthReadActive) {
                    currentRT->EndDepthShaderRead();
                }
            }

            if (globalChain_.HasAny()) {
                finalSceneRT = globalChain_.Execute(*finalSceneRT, quad_, commonParams_);
            }

            RenderTarget2D* bloomRT = ApplyBloom(*finalSceneRT);
            if (bloomRT != nullptr && bloomRT->GetResource() != nullptr) {
                finalSceneRT->Rebind();
                if (!quad_.SetOutputFormat(finalSceneRT->GetFormat())) {
                    DEBUGLOG::PushRenderError(std::string("[PostSystem][BloomComposite][ERROR] SetOutputFormat failed. dst=") +
                        finalSceneRT->GetDebugName() +
                        " dstFormat=" +
                        GFX::FormatToString(finalSceneRT->GetFormat()) +
                        "\n" + quad_.DumpState() +
                        "\n" + DumpFrameState());
                    GFX::DumpD3D12InfoQueue(context_.device, "Bloom composite SetOutputFormat failed");
                } else {
                    if (GFX::GetGfxDebugConfig().verbosePostLog) {
                        HIKARI_LOG_INFO(std::string("[PostSystem][BloomComposite] dst=") +
                            finalSceneRT->GetDebugName() +
                            " dstFormat=" +
                            GFX::FormatToString(finalSceneRT->GetFormat()) +
                            " src=" +
                            bloomRT->GetDebugName() +
                            " blend=Additive");
                    }
                    quad_.DrawBlended(bloomRT->GetSrvHeap(), bloomRT->GetSrvGpu(), BlendOption::Additive);
                }
                finalSceneRT->EndCapture();
            }

            if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
                HIKARI_LOG_INFO(std::string("[PostSystem][FramePath] globalPost=") +
                    (globalChain_.HasAny() ? "on" : "off") +
                    " bloom=" +
                    ((bloomRT != nullptr && bloomRT->GetResource() != nullptr) ? "on" : "off") +
                    " fxaaStage=" +
                    (RENDER3D::IsFxaaAntiAliasingMode(
                        renderQuality.antiAliasingMode) ? "afterTone" : "off") +
                    " toneMapping=" +
                    (toneMappingSettings_.enabled ? "on" : "off") +
                    " transition=" +
                    (transitionActive_ ? "on" : "off") +
                    " useLighting=" +
                    (useLighting_ ? "on" : "off") +
                    " finalRT=" +
                    (finalSceneRT ? finalSceneRT->GetDebugName() : "<null>"));
            }

            return finalSceneRT;
        }

        RenderTarget2D* PostSystem::ResolveFinalSceneToLdr(RenderTarget2D& finalSceneRT, DXGI_FORMAT outputFormat)
        {
            if (!EnsureToneMappedLdrRTSize(finalSceneRT.GetWidth(), finalSceneRT.GetHeight(), outputFormat)) {
                LogFrameState("ToneMapped LDR RT failed");
                return nullptr;
            }

            const bool useTransition = transitionActive_ && transitionEffect_;
            if (!useTransition && !EnsureToneMappingEffect()) {
                return nullptr;
            }

            toneMappedLdrRT_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);

            if (!quad_.SetOutputFormat(toneMappedLdrRT_.GetFormat())) {
                toneMappedLdrRT_.EndCapture();
                LogFrameState("ToneMapping output format failed");
                GFX::DumpD3D12InfoQueue(context_.device, "ToneMapping output format failed");
                return nullptr;
            }

            if (useTransition) {
                quad_.SetInputTexture(finalSceneRT.GetSrvHeap(), finalSceneRT.GetSrvGpu());
                transitionEffect_->ApplyCommonParams(transitionParams_);
                if (!transitionEffect_->BindAndDraw(quad_)) {
                    toneMappedLdrRT_.EndCapture();
                    LogFrameState("Transition BindAndDraw failed");
                    GFX::DumpD3D12InfoQueue(context_.device, "Transition BindAndDraw failed");
                    return nullptr;
                }
            } else {
                toneMappingParams_ = commonParams_;
                toneMappingParams_.user[0] = {
                    toneMappingSettings_.enabled ? 1.0f : 0.0f,
                    (std::max)(0.0f, toneMappingSettings_.exposure),
                    (std::max)(0.01f, toneMappingSettings_.gamma),
                    static_cast<float>(toneMappingSettings_.mode)
                };
                if (GFX::GetGfxDebugConfig().verbosePostLog) {
                    HIKARI_LOG_INFO(std::string("[PostSystem][ToneMapping] Begin input=") +
                        finalSceneRT.GetDebugName() +
                        " inputFormat=" +
                        GFX::FormatToString(finalSceneRT.GetFormat()) +
                        " outputFormat=" +
                        GFX::FormatToString(toneMappedLdrRT_.GetFormat()) +
                        " exposure=" +
                        std::to_string(toneMappingParams_.user[0].y) +
                        " gamma=" +
                        std::to_string(toneMappingParams_.user[0].z) +
                        " mode=" +
                        std::to_string(toneMappingSettings_.mode));
                }
                quad_.SetInputTexture(finalSceneRT.GetSrvHeap(), finalSceneRT.GetSrvGpu());
                toneMappingEffect_->ApplyCommonParams(toneMappingParams_);
                if (!toneMappingEffect_->BindAndDraw(quad_)) {
                    toneMappedLdrRT_.EndCapture();
                    DEBUGLOG::PushRenderError("[PostSystem][ToneMapping][ERROR] BindAndDraw failed.");
                    LogFrameState("ToneMapping BindAndDraw failed");
                    GFX::DumpD3D12InfoQueue(context_.device, "ToneMapping BindAndDraw failed");
                    return nullptr;
                }
            }

            if (useLighting_) {
                quad_.DrawBlended(lightRT_.GetSrvHeap(), lightRT_.GetSrvGpu(), BlendOption::Multiply);
            }

            toneMappedLdrRT_.EndCapture();

            RenderTarget2D* resolvedRT = &toneMappedLdrRT_;
            const RENDER3D::RenderQualitySettings& renderQuality =
                RENDER3D::GetRenderQualitySettings();
            RenderTarget2D* fxaaRT =
                RENDER3D::IsFxaaAntiAliasingMode(renderQuality.antiAliasingMode)
                    ? ApplyFxaa(*resolvedRT)
                    : nullptr;
            if (fxaaRT != nullptr && fxaaRT->GetResource() != nullptr) {
                resolvedRT = fxaaRT;
            }

            if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
                HIKARI_LOG_INFO(std::string("[PostSystem][LdrResolve] input=") +
                    finalSceneRT.GetDebugName() +
                    " ldr=" +
                    toneMappedLdrRT_.GetDebugName() +
                    " fxaa=" +
                    ((fxaaRT != nullptr && fxaaRT->GetResource() != nullptr) ? "on" : "off") +
                    " resolved=" +
                    (resolvedRT ? resolvedRT->GetDebugName() : "<null>"));
            }

            if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
                LogFrameState(dumpNextFrame_ ? "Requested frame dump" : "Verbose post log");
                dumpNextFrame_ = false;
            }

            return resolvedRT;
        }

        bool PostSystem::DrawResolvedSceneToCurrentTarget(RenderTarget2D& resolvedSceneRT, DXGI_FORMAT outputFormat)
        {
            if (resolvedSceneRT.GetResource() == nullptr) {
                LogFrameState("Resolved scene RT null");
                return false;
            }

            if (!quad_.SetOutputFormat(outputFormat)) {
                LogFrameState("Resolved scene output format failed");
                GFX::DumpD3D12InfoQueue(context_.device, "Resolved scene output format failed");
                return false;
            }

            quad_.DrawFullscreen(resolvedSceneRT.GetSrvHeap(), resolvedSceneRT.GetSrvGpu());
            return true;
        }

        void PostSystem::BindBackBufferFullViewport()
        {
            ID3D12GraphicsCommandList* cmd = context_.cmdList;
            if (!cmd) {
                return;
            }

            cmd->OMSetRenderTargets(1, &context_.rtv, FALSE, nullptr);

            const float width = static_cast<float>((std::max)(context_.backBufferWidth, 1));
            const float height = static_cast<float>((std::max)(context_.backBufferHeight, 1));
            D3D12_VIEWPORT vp{ 0.0f, 0.0f, width, height, 0.0f, 1.0f };
            D3D12_RECT sc{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
            cmd->RSSetViewports(1, &vp);
            cmd->RSSetScissorRects(1, &sc);
        }

        bool PostSystem::EndSceneCaptureToEditorViewport()
        {
            if (!sceneCaptureActive_) {
                return IsEditorViewportReady();
            }

            RenderTarget2D* finalSceneRT = EndSceneCaptureAndResolveFinal();
            sceneCaptureActive_ = false;
            if (finalSceneRT == nullptr || context_.cmdList == nullptr) {
                BindBackBufferFullViewport();
                editorViewportReady_ = false;
                return false;
            }

            GFX::GPU_PROFILE::ScopedGpuTimer gpuGameViewResolve(
                context_.cmdList,
                GFX::GPU_PROFILE::Pass::GameViewResolve);

            RenderTarget2D* resolvedSceneRT = ResolveFinalSceneToLdr(*finalSceneRT, DXGI_FORMAT_R8G8B8A8_UNORM);
            if (resolvedSceneRT == nullptr || resolvedSceneRT->GetResource() == nullptr) {
                BindBackBufferFullViewport();
                editorViewportReady_ = false;
                return false;
            }

            const int captureW = resolvedSceneRT->GetWidth();
            const int captureH = resolvedSceneRT->GetHeight();
            EnsureEditorViewportRTSize(captureW, captureH);
            if (!editorViewportRT_.GetResource() || !editorViewportRT_.IsInitialized()) {
                BindBackBufferFullViewport();
                editorViewportReady_ = false;
                return false;
            }

            editorViewportRT_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
            const bool drew = DrawResolvedSceneToCurrentTarget(*resolvedSceneRT, DXGI_FORMAT_R8G8B8A8_UNORM);
            editorViewportRT_.EndCapture();
            RefreshEditorViewportSrvDescriptor();
            BindBackBufferFullViewport();
            editorViewportReady_ = drew &&
                editorViewportSrvGpu_.ptr != 0 &&
                IsEditorViewportTextureCurrent();
            return editorViewportReady_;
        }

        void PostSystem::EndSceneCaptureAndPresent()
        {
            if (!sceneCaptureActive_) {
                return;
            }

            RenderTarget2D* finalSceneRT = EndSceneCaptureAndResolveFinal();
            sceneCaptureActive_ = false;
            if (finalSceneRT == nullptr) {
                BindBackBufferFullViewport();
                return;
            }

            auto* cmd = context_.cmdList;
            if (!cmd) {
                LogFrameState("EndSceneCaptureAndPresent cmd null");
                return;
            }

            RenderTarget2D* resolvedSceneRT = ResolveFinalSceneToLdr(*finalSceneRT, DXGI_FORMAT_R8G8B8A8_UNORM);
            if (resolvedSceneRT == nullptr || resolvedSceneRT->GetResource() == nullptr) {
                BindBackBufferFullViewport();
                return;
            }

            cmd->OMSetRenderTargets(1, &context_.rtv, FALSE, nullptr);

            const auto letterbox = ComputeLetterboxRect(context_.backBufferWidth, context_.backBufferHeight);
            D3D12_VIEWPORT vp{ letterbox.x, letterbox.y, letterbox.width, letterbox.height, 0.0f, 1.0f };
            D3D12_RECT sc{
                static_cast<LONG>(letterbox.x),
                static_cast<LONG>(letterbox.y),
                static_cast<LONG>(letterbox.x + letterbox.width),
                static_cast<LONG>(letterbox.y + letterbox.height)
            };
            cmd->RSSetViewports(1, &vp);
            cmd->RSSetScissorRects(1, &sc);

            DrawResolvedSceneToCurrentTarget(*resolvedSceneRT, DXGI_FORMAT_R8G8B8A8_UNORM);
        }

        void PostSystem::BeginLayer(PostChain& chain, float r, float g, float b, float a)
        {
            if (!initialized_) Initialize(context_);
            if (rtStack_.empty()) {
                BeginSceneCapture();
            }

            RenderTarget2D* prevRT = rtStack_.top().rt;
            int w = prevRT->GetWidth();
            int h = prevRT->GetHeight();

            chain.UpdateContext(context_);
            chain.PrepareBuffers(w, h);
            RenderTarget2D* layerRT = chain.GetPing();

            prevRT->EndCapture();
            layerRT->BeginCapture(r, g, b, a);
            rtStack_.push({ layerRT, &chain });
        }

        void PostSystem::EndLayer(BlendOption blendMode)
        {
            if (rtStack_.size() <= 1) { return; }

            LayerInfo currentLayer = rtStack_.top();
            rtStack_.pop();

            currentLayer.rt->EndCapture();

            RenderTarget2D* processedRT = currentLayer.rt;
            if (currentLayer.chain && currentLayer.chain->HasAny()) {
                processedRT = currentLayer.chain->Execute(*currentLayer.rt, quad_, commonParams_);
            }

            LayerInfo prevLayer = rtStack_.top();
            prevLayer.rt->Rebind();

            auto* cmd = context_.cmdList;
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = prevLayer.rt->GetRtvHandle();
            cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            if (!quad_.SetOutputFormat(prevLayer.rt->GetFormat())) {
                LogFrameState("EndLayer SetOutputFormat failed");
                GFX::DumpD3D12InfoQueue(context_.device, "EndLayer SetOutputFormat failed");
                return;
            }
            quad_.DrawBlended(processedRT->GetSrvHeap(), processedRT->GetSrvGpu(), blendMode);
        }

    } // POST
} // HIKARI
