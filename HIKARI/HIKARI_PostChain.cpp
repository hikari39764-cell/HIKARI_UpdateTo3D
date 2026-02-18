#include "HIKARI_PostChain.h"
#include "HIKARI_PostQuadDrawer.h"
#include "HIKARI_PostEffect.h"

#include <base/DirectXCommon.h>

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
                ping_.Init(w, h);
                pong_.Init(w, h);
                tempsInitialized_ = true;
                return;
            }

            if (ping_.GetWidth() != w || ping_.GetHeight() != h) {
                ping_.Finalize();
                ping_.Init(w, h);
            }
            if (pong_.GetWidth() != w || pong_.GetHeight() != h) {
                pong_.Finalize();
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
