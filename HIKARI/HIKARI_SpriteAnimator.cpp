#include "HIKARI_SpriteAnimator.h"

namespace HIKARI {
    namespace ANIM {

        SpriteAnimator::SpriteAnimator() {
        }

        SpriteAnimator::SpriteAnimator(const SpriteAnimDesc& desc) {
            Reset(desc);
        }

        void SpriteAnimator::Reset(const SpriteAnimDesc& desc) {
            desc_ = desc;
            time_ = 0.0f;
            currentFrame_ = 0;
            playing_ = true;
            paused_ = false;
            finished_ = false;
        }

        void SpriteAnimator::Update(float deltaSeconds) {
            if (!playing_ || paused_) {
                return;
            }

            if (desc_.totalFrames <= 0 || desc_.fps <= 0.0f) {
                return;
            }

            time_ += deltaSeconds;
            float frameFloat = time_ * desc_.fps;
            int newFrame = static_cast<int>(frameFloat);

            if (newFrame >= desc_.totalFrames) {
                if (desc_.loop) {
                    // ÉãÅ[Évçƒê∂
                    int loopFrames = desc_.totalFrames;
                    if (loopFrames > 0) {
                        newFrame = newFrame % loopFrames;
                    } else {
                        newFrame = 0;
                    }
                    finished_ = false;
                } else {
                    // àÍâÒÇ´ÇËçƒê∂
                    newFrame = desc_.totalFrames - 1;
                    playing_ = false;
                    finished_ = true;
                }
            }

            currentFrame_ = newFrame;
        }

        void SpriteAnimator::Draw(
            const HIKARI::Transform2D& t,
            RENDERER::CameraMode cam,
            unsigned int rgba) const
        {
            if (desc_.textureName.empty()) {
                return;
            }

            if (desc_.totalFrames <= 0) {
                return;
            }

            RENDERER::DrawSpriteFrame(
                desc_.textureName,
                desc_.sheet,
                currentFrame_,
                t,
                cam,
                rgba
            );
        }

        void SpriteAnimator::Play() {
            time_ = 0.0f;
            currentFrame_ = 0;
            playing_ = true;
            paused_ = false;
            finished_ = false;
        }

        void SpriteAnimator::Stop() {
            time_ = 0.0f;
            currentFrame_ = 0;
            playing_ = false;
            paused_ = false;
            finished_ = false;
        }

        void SpriteAnimator::Pause() {
            paused_ = true;
        }

        void SpriteAnimator::Resume() {
            paused_ = false;
        }

    } // namespace ANIM
} // namespace HIKARI
