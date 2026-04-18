#include "HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "HIKARI_Utility.h"
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
        CommonParams PostSystem::commonParams_{};
        float PostSystem::elapsedTime_ = 0.0f;
        std::stack<PostSystem::LayerInfo> PostSystem::rtStack_{};
        float PostSystem::ambientColor_[3] = { 1.0f, 1.0f, 1.0f };
        bool PostSystem::useLighting_ = false;
        std::string PostSystem::activeGlobalProfileId_{};
        PostProfile PostSystem::activeGlobalProfile_{};
        std::vector<std::unique_ptr<PostEffect>> PostSystem::activeGlobalEffects_{};
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
            if (initialized_) return;
            quad_.Init(context_);
            initialized_ = true;
        }

        void PostSystem::UpdateContext(const GFX::Context& ctx)
        {
            context_ = ctx;
            PostEffect::UpdateContext(ctx);
            globalChain_.UpdateContext(ctx);
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

            const bool sceneInvalid = (!sceneRT_.GetResource() || sceneRT_.GetWidth() != w || sceneRT_.GetHeight() != h || !sceneRT_.HasDepth());
            if (sceneInvalid) {
                sceneRT_.Finalize();
                sceneRT_.Init(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
            }

            if (!lightRT_.GetResource() || lightRT_.GetWidth() != w || lightRT_.GetHeight() != h) {
                lightRT_.Finalize();
                lightRT_.Init(w, h);
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


            if (transitionActive_ && transitionEffect_) {
                quad_.SetInputTexture(finalSceneRT->GetSrvHeap(), finalSceneRT->GetSrvGpu());
                transitionEffect_->ApplyCommonParams(transitionParams_);
                transitionEffect_->BindAndDraw(quad_);
            } else {
                quad_.DrawFullscreen(finalSceneRT->GetSrvHeap(), finalSceneRT->GetSrvGpu());
            }

            if (useLighting_) {
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

            quad_.DrawBlended(processedRT->GetSrvHeap(), processedRT->GetSrvGpu(), blendMode);
        }

    } // POST
} // HIKARI
