#pragma once
#include <string>
#include "HIKARI_Transform2D.h"
#include "HIKARI_Renderer.h"

namespace HIKARI {
    namespace ANIM {

        struct SpriteAnimDesc {
            std::string textureName; 
            RENDERER::SpriteSheetInfo sheet; 

            int   totalFrames = 1; 
            float fps = 12.0f; 
            bool  loop = true;  
        };


        class SpriteAnimator {
        public:
            SpriteAnimator();
            explicit SpriteAnimator(const SpriteAnimDesc& desc);

            void Reset(const SpriteAnimDesc& desc);

            void Update(float deltaSeconds);

            void Draw(
                const HIKARI::Transform2D& t,
                RENDERER::CameraMode cam = RENDERER::CameraMode::Inherit,
                unsigned int rgba = 0xFFFFFFFF) const;

            // 嵞惗惂屼
            void Play(); 
            void Stop(); 
            void Pause(); 
            void Resume(); 

            // 忬懺庢摼
            bool IsPlaying() const { return playing_ && !paused_; }
            bool IsPaused()  const { return paused_; }
            bool IsFinished() const { return finished_; }

            int   GetCurrentFrame() const { return currentFrame_; }
            float GetTime() const { return time_; }

        private:
            SpriteAnimDesc desc_{};
            float time_ = 0.0f;
            int   currentFrame_ = 0;

            bool playing_ = false;
            bool paused_ = false;
            bool finished_ = false;
        };

    } // namespace ANIM
} // namespace HIKARI
