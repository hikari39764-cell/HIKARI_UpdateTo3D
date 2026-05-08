#pragma once

#include <string>
#include <string_view>

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class AnimatorComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "AnimatorComponent"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;
        void RenderImGui() override;

        void Play(std::string clip, bool loop = true, bool restart = true);
        void PlayCurrent(bool restart = true);
        void PlayOnce(std::string clip, bool restart = true);
        void Pause();
        void Resume();
        void Stop();
        void ResetTime();
        void Advance(float deltaTimeSec, float clipDurationSec = -1.0f);

        void SetClip(std::string clip);
        const std::string& GetClip() const;

        void SetTime(float timeSec);
        float GetTime() const;

        void SetSpeed(float speed);
        float GetSpeed() const;

        void SetLoop(bool loop);
        bool GetLoop() const;

        void SetAutoPlay(bool autoPlay);
        bool GetAutoPlay() const;

        void SetPlaying(bool playing);
        bool IsPlaying() const;

        bool IsFinished() const;
        bool IsFinished(float clipDurationSec) const;

    private:
        std::string clip_{};
        float timeSec_ = 0.0f;
        float speed_ = 1.0f;
        bool loop_ = true;
        bool autoPlay_ = true;
        bool playing_ = true;
        bool finished_ = false;
    };

} // namespace HIKARI
