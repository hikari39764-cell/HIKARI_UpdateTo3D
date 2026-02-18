#include "HIKARI_GlowManager.h"
#include "HIKARI_PostSystem.h"
#include <cmath>
#include <algorithm> // for std::max, std::min

namespace HIKARI {
    namespace POST {

        bool GlowManager::initialized_ = false;
        PostChain GlowManager::glowChain_{};
        PostEffect* GlowManager::blurSmallX_ = nullptr;
        PostEffect* GlowManager::blurSmallY_ = nullptr;
        PostEffect* GlowManager::blurBigX_ = nullptr;
        PostEffect* GlowManager::blurBigY_ = nullptr;

        float GlowManager::currentIntensity_ = 1.0f;
        float GlowManager::currentRadius_ = 4.0f;

        void GlowManager::Init()
        {
            if (initialized_) return;

            const wchar_t* shaderPath = L"shaders/glow.hlsl";

            blurSmallX_ = new PostEffect();
            blurSmallY_ = new PostEffect();
            blurBigX_ = new PostEffect();
            blurBigY_ = new PostEffect();

            bool loaded = true;
            loaded &= blurSmallX_->LoadPixelShader(shaderPath);
            loaded &= blurSmallY_->LoadPixelShader(shaderPath);
            loaded &= blurBigX_->LoadPixelShader(shaderPath);
            loaded &= blurBigY_->LoadPixelShader(shaderPath);

            if (!loaded) return;

            glowChain_.Clear();
            glowChain_.Add(blurSmallX_);
            glowChain_.Add(blurSmallY_);
            glowChain_.Add(blurBigX_);
            glowChain_.Add(blurBigY_);

            initialized_ = true;
            UpdateParams();
        }

        void GlowManager::Finalize()
        {
            if (!initialized_) return;
            glowChain_.Clear();
            delete blurSmallX_; blurSmallX_ = nullptr;
            delete blurSmallY_; blurSmallY_ = nullptr;
            delete blurBigX_;   blurBigX_ = nullptr;
            delete blurBigY_;   blurBigY_ = nullptr;
            initialized_ = false;
        }

        void GlowManager::SetIntensity(float intensity)
        {
            currentIntensity_ = intensity;
            PostSystem::SetIntensity(currentIntensity_);
        }

        void GlowManager::SetRadius(float radius)
        {
            currentRadius_ = radius;
            UpdateParams();
        }

        void GlowManager::UpdateParams()
        {
            if (!initialized_) return;

            float smallStep = 1.5f;


            float bigStep = 2.0f + (currentRadius_ * 0.5f);

            if (bigStep > 8.0f) bigStep = 8.0f;

            // 赋值
            blurSmallX_->SetUser(0, { 1.0f, 0.0f, smallStep, 0.0f });
            blurSmallY_->SetUser(0, { 0.0f, 1.0f, smallStep, 0.0f });

            blurBigX_->SetUser(0, { 1.0f, 0.0f, bigStep, 0.0f });
            blurBigY_->SetUser(0, { 0.0f, 1.0f, bigStep, 0.0f });
        }
        void GlowManager::Begin()
        {
            if (!initialized_) Init();
            PostSystem::BeginLayer(glowChain_, 0.0f, 0.0f, 0.0f, 0.0f);
        }

        void GlowManager::End()
        {
            if (!initialized_) return;

            PostSystem::EndLayer(BlendOption::Additive);
        }

    } // POST
} // HIKARI