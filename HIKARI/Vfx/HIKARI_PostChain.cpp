#include "HIKARI_PostChain.h"
#include "HIKARI_PostQuadDrawer.h"
#include "HIKARI_PostEffect.h"

namespace HIKARI {
    namespace POST {

        void PostChain::Clear()
        {
            effects_.clear();
        }

        void PostChain::Add(PostEffect* effect)
        {
            if (!effect) {
                return;
            }
            effects_.push_back(effect);
        }

        void PostChain::UpdateContext(const GFX::Context& ctx)
        {
            context_ = ctx;
            ping_.UpdateContext(ctx);
            pong_.UpdateContext(ctx);
        }

        void PostChain::Finalize()
        {
            ping_.Finalize();
            pong_.Finalize();
            tempsInitialized_ = false;
            effects_.clear();
        }

        void PostChain::EnsureTempSize(int w, int h)
        {
            if (w <= 0 || h <= 0) {
                return;
            }

            if (!tempsInitialized_ || ping_.GetResource() == nullptr || pong_.GetResource() == nullptr) {
                ping_.UpdateContext(context_);
                pong_.UpdateContext(context_);
                const bool okPing = ping_.Init(w, h);
                const bool okPong = pong_.Init(w, h);
                tempsInitialized_ = okPing && okPong;
                return;
            }

            if (ping_.GetWidth() != w || ping_.GetHeight() != h) {
                ping_.Finalize();
                ping_.UpdateContext(context_);
                ping_.Init(w, h);
            }
            if (pong_.GetWidth() != w || pong_.GetHeight() != h) {
                pong_.Finalize();
                pong_.UpdateContext(context_);
                pong_.Init(w, h);
            }
        }
        void PostChain::PrepareBuffers(int w, int h)
        {
            EnsureTempSize(w, h);
        }

        RenderTarget2D* PostChain::Execute(RenderTarget2D& src, QuadDrawer& quad, const CommonParams& params)
        {
            if (effects_.empty()) {
                return &src;
            }


            int w = src.GetWidth();
            int h = src.GetHeight();
            EnsureTempSize(w, h);
            if (ping_.GetResource() == nullptr || pong_.GetResource() == nullptr) {
                return &src;
            }

            RenderTarget2D* cur = &src;

            for (size_t i = 0; i < effects_.size(); i++) {
                RenderTarget2D* dst = nullptr;


                if (cur == &ping_) {
                    dst = &pong_; 
                } else if (cur == &pong_) {
                    dst = &ping_; 
                } else {

                    dst = &ping_;
                }

                dst->BeginCapture(0, 0, 0, 0); 

 
                quad.SetInputTexture(cur->GetSrvHeap(), cur->GetSrvGpu());


                effects_[i]->ApplyCommonParams(params);
                effects_[i]->BindAndDraw(quad);

                dst->EndCapture();

                cur = dst;
            }

            return cur;
        }

    } // POST
} // HIKARI
