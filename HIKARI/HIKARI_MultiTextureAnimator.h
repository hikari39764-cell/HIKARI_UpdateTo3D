#pragma once
#include <string>
#include <vector>
#include "HIKARI_Transform2D.h"
#include "HIKARI_Renderer.h"

namespace HIKARI {
    namespace ANIM {

    
        struct MultiTexAnimDesc {
            std::vector<std::string> textureNames; 
            int   frameWidth = 0; 
            int   frameHeight = 0; 
            float fps = 12.0f; 
            bool  loop = true; 
        };

        class MultiTextureAnimator {
        public:
            MultiTextureAnimator();
            explicit MultiTextureAnimator(const MultiTexAnimDesc& desc);

            void Reset(const MultiTexAnimDesc& desc);

            void Update(float deltaSeconds);

            void Draw(
                const HIKARI::Transform2D& t,
                HIKARI::RENDERER::CameraMode cam = HIKARI::RENDERER::CameraMode::Inherit,
                unsigned int rgba = 0xFFFFFFFF) const;

            void Play();
            void Stop(); 
            void Pause(); 
            void Resume(); 

            bool IsPlaying() const { return playing_ && !paused_; }
            bool IsPaused()  const { return paused_; }
            bool IsFinished() const { return finished_; }
            int  GetCurrentFrame() const { return currentFrame_; }

        private:
            MultiTexAnimDesc desc_{};
            float time_ = 0.0f;
            int   currentFrame_ = 0;

            bool playing_ = false;
            bool paused_ = false;
            bool finished_ = false;
        };

        MultiTexAnimDesc LoadAnimationFolder(
            const std::string& groupName,
            const std::string& folderPath,
            int frameWidth,
            int frameHeight,
            float fps,
            bool loop);

    } // namespace ANIM
} // namespace HIKARI
