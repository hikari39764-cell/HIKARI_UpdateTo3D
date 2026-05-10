#include "HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "HIKARI_Utility.h"
#include <algorithm>
#include <cassert>

namespace HIKARI {
    namespace POST {

        namespace {
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
        RenderTarget2D PostSystem::lightRT_{};
        QuadDrawer PostSystem::quad_{};
        PostChain PostSystem::globalChain_{};
        PostChain PostSystem::bloomChain_{};
        CommonParams PostSystem::commonParams_{};
        float PostSystem::elapsedTime_ = 0.0f;
        std::stack<PostSystem::LayerInfo> PostSystem::rtStack_{};
        float PostSystem::ambientColor_[3] = { 1.0f, 1.0f, 1.0f };
        bool PostSystem::useLighting_ = false;
        std::string PostSystem::activeGlobalProfileId_{};
        PostProfile PostSystem::activeGlobalProfile_{};
        std::vector<std::unique_ptr<PostEffect>> PostSystem::activeGlobalEffects_{};
        std::vector<std::unique_ptr<PostEffect>> PostSystem::activeBloomEffects_{};
        BloomSettings PostSystem::bloomSettings_{};
        ToneMappingSettings PostSystem::toneMappingSettings_{};
        PostSystem::BloomDebugStats PostSystem::bloomDebugStats_{};
        uint32_t PostSystem::activeBloomBlurPairCount_ = 0;
        std::unique_ptr<PostEffect> PostSystem::toneMappingEffect_{};
        CommonParams PostSystem::toneMappingParams_{};
        bool PostSystem::transitionActive_ = false;
        std::string PostSystem::activeTransitionProfileId_{};
        TransitionProfile PostSystem::activeTransitionProfile_{};
        std::unique_ptr<PostEffect> PostSystem::transitionEffect_{};
        CommonParams PostSystem::transitionParams_{};


        void PostSystem::Initialize(const GFX::Context& ctx)
        {
            context_ = ctx;
            PostEffect::UpdateContext(ctx);
            globalChain_.UpdateContext(ctx);
            bloomChain_.UpdateContext(ctx);
            if (initialized_) return;
            quad_.Init(context_);
            initialized_ = true;
        }

        void PostSystem::UpdateContext(const GFX::Context& ctx)
        {
            context_ = ctx;
            PostEffect::UpdateContext(ctx);
            globalChain_.UpdateContext(ctx);
            bloomChain_.UpdateContext(ctx);
            quad_.UpdateContext(ctx);
            sceneRT_.UpdateContext(ctx);
            lightRT_.UpdateContext(ctx);
        }

        void PostSystem::Shutdown()
        {
            if (!initialized_) return;
            ClearGlobalProfile();
            ClearTransitionState();
            globalChain_.Finalize();
            bloomChain_.Finalize();
            activeBloomEffects_.clear();
            toneMappingEffect_.reset();
            sceneRT_.Finalize();
            lightRT_.Finalize();
            quad_.Finalize();

            while (!rtStack_.empty()) rtStack_.pop();
            initialized_ = false;
        }

        void PostSystem::UpdateCommonParams(float deltaTime)
        {
            if (!initialized_) { Initialize(context_); }

            commonParams_.resolutionX = static_cast<float>(kScreenW);
            commonParams_.resolutionY = static_cast<float>(kScreenH);

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

                activeGlobalEffects_.clear();
                activeGlobalEffects_.reserve(loadedProfile.passes.size());
                globalChain_.Clear();
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
            activeGlobalEffects_.clear();
            globalChain_.Clear();
        }

        void PostSystem::SetBloomSettings(const BloomSettings& settings) {
            bloomSettings_ = settings;
        }

        void PostSystem::SetToneMappingSettings(const ToneMappingSettings& settings) {
            toneMappingSettings_ = settings;
        }

        const PostSystem::BloomDebugStats& PostSystem::GetBloomDebugStats() {
            return bloomDebugStats_;
        }

        bool PostSystem::EnsureToneMappingEffect() {
            if (toneMappingEffect_) {
                return true;
            }

            auto effect = std::make_unique<PostEffect>();
            if (!effect->LoadPixelShader(L"HIKARI/Shaders/Post_ToneMappingPS.hlsl")) {
                return false;
            }
            toneMappingEffect_ = std::move(effect);
            return true;
        }

        bool PostSystem::EnsureBloomEffects(uint32_t blurPairCount) {
            blurPairCount = std::clamp<uint32_t>(blurPairCount, 1u, 5u);
            if (activeBloomBlurPairCount_ == blurPairCount && !activeBloomEffects_.empty() && bloomChain_.HasAny()) {
                return true;
            }

            activeBloomEffects_.clear();
            bloomChain_.Clear();
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
                return false;
            }

            for (uint32_t i = 0; i < blurPairCount; ++i) {
                auto blurH = addEffect(L"HIKARI/Shaders/Post_BloomBlurHPS.hlsl");
                auto blurV = addEffect(L"HIKARI/Shaders/Post_BloomBlurVPS.hlsl");
                if (!blurH || !blurV) {
                    activeBloomEffects_.clear();
                    bloomChain_.Clear();
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

        void PostSystem::EnsureSceneRTSize()
        {
            int w = kScreenW;
            int h = kScreenH;
            if (w <= 0 || h <= 0) return;

            sceneRT_.UpdateContext(context_);
            lightRT_.UpdateContext(context_);

            const bool sceneInvalid =
                (!sceneRT_.GetResource() ||
                    sceneRT_.GetWidth() != w ||
                    sceneRT_.GetHeight() != h ||
                    !sceneRT_.HasDepth());

            if (sceneInvalid) {
                sceneRT_.Finalize();
                sceneRT_.Init(
                    w, h,
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    true,
                    { 0.0f, 0.0f, 0.0f, 1.0f }   // sceneRT 常用黑底不透明
                );
            }

            if (!lightRT_.GetResource() ||
                lightRT_.GetWidth() != w ||
                lightRT_.GetHeight() != h) {
                lightRT_.Finalize();
                lightRT_.Init(
                    w, h,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    false,
                    { 1.0f, 1.0f, 1.0f, 1.0f }   // lightRT 默认 ambientColor 初始值就是白
                );
            }
        }

        void PostSystem::BeginSceneCapture()
        {
            if (!initialized_) Initialize(context_);

            EnsureSceneRTSize();
            UpdateCommonParams(0.0f);


            useLighting_ = false;

            while (!rtStack_.empty()) rtStack_.pop();


            rtStack_.push({ &sceneRT_, nullptr });

            sceneRT_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
        }

        bool PostSystem::RebindCurrentRenderTarget()
        {
            if (!initialized_ || rtStack_.empty() || rtStack_.top().rt == nullptr) {
                return false;
            }

            rtStack_.top().rt->Rebind();
            return true;
        }


        void PostSystem::BeginLightCapture()
        {
            if (!initialized_) return;


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

        void PostSystem::EndSceneCaptureAndPresent()
        {
            if (!initialized_) return;
            if (rtStack_.empty()) return;


            RenderTarget2D* currentRT = rtStack_.top().rt;
            currentRT->EndCapture();
            rtStack_.pop();


            RenderTarget2D* finalSceneRT = currentRT;
            if (globalChain_.HasAny()) {
                finalSceneRT = globalChain_.Execute(*currentRT, quad_, commonParams_);
            }

            RenderTarget2D* bloomRT = ApplyBloom(*finalSceneRT);
            if (bloomRT != nullptr && bloomRT->GetResource() != nullptr) {
                finalSceneRT->Rebind();
                quad_.SetOutputFormat(finalSceneRT->GetFormat());
                quad_.DrawBlended(bloomRT->GetSrvHeap(), bloomRT->GetSrvGpu(), BlendOption::Additive);
                finalSceneRT->EndCapture();
            }


            auto* cmd = context_.cmdList;
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


            RenderTarget2D* presentSourceRT = finalSceneRT;

            quad_.SetOutputFormat(DXGI_FORMAT_R8G8B8A8_UNORM);

            if (transitionActive_ && transitionEffect_) {
                quad_.SetInputTexture(presentSourceRT->GetSrvHeap(), presentSourceRT->GetSrvGpu());
                transitionEffect_->ApplyCommonParams(transitionParams_);
                transitionEffect_->BindAndDraw(quad_);
            } else if (EnsureToneMappingEffect()) {
                toneMappingParams_ = commonParams_;
                toneMappingParams_.user[0] = {
                    toneMappingSettings_.enabled ? 1.0f : 0.0f,
                    (std::max)(0.0f, toneMappingSettings_.exposure),
                    (std::max)(0.001f, toneMappingSettings_.gamma),
                    static_cast<float>(toneMappingSettings_.mode)
                };
                quad_.SetInputTexture(presentSourceRT->GetSrvHeap(), presentSourceRT->GetSrvGpu());
                toneMappingEffect_->ApplyCommonParams(toneMappingParams_);
                toneMappingEffect_->BindAndDraw(quad_);
            } else {
                quad_.DrawFullscreen(presentSourceRT->GetSrvHeap(), presentSourceRT->GetSrvGpu());
            }

            if (useLighting_) {
                quad_.SetOutputFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
                quad_.DrawBlended(lightRT_.GetSrvHeap(), lightRT_.GetSrvGpu(), BlendOption::Multiply);
            }
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

            quad_.SetOutputFormat(prevLayer.rt->GetFormat());
            quad_.DrawBlended(processedRT->GetSrvHeap(), processedRT->GetSrvGpu(), blendMode);
        }

    } // POST
} // HIKARI
