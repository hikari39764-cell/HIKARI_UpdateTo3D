#pragma once
#include <vector>

#include "HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {
    namespace POST {

        class QuadDrawer;
        class PostEffect;

        class PostChain
        {
        public:
            void Clear();
            void Add(PostEffect* effect);
            void UpdateContext(const GFX::Context& ctx);

            bool HasAny() const { return !effects_.empty(); }
            RenderTarget2D* Execute(RenderTarget2D& src, QuadDrawer& quad, const CommonParams& params);
            RenderTarget2D* GetPing() { return &ping_; }
            void PrepareBuffers(int w, int h);

            void Finalize();

        private:
            void EnsureTempSize(int w, int h);

        private:
            std::vector<PostEffect*> effects_;

            RenderTarget2D ping_{};
            RenderTarget2D pong_{};
            bool tempsInitialized_ = false;
            GFX::Context context_{};
        };

    } // POST
} // HIKARI
