#include "HIKARI_AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        float ClampTime(float value) {
            return std::max(0.0f, value);
        }
    }

    void AnimatorComponent::Serialize(nlohmann::json& out) const {
        out["clip"] = clip_;
        out["timeSec"] = timeSec_;
        out["speed"] = speed_;
        out["loop"] = loop_;
        out["autoPlay"] = autoPlay_;
        out["playing"] = playing_;
        out["finished"] = finished_;
    }

    void AnimatorComponent::Deserialize(const nlohmann::json& in) {
        clip_ = in.value("clip", clip_);
        timeSec_ = ClampTime(in.value("timeSec", timeSec_));
        speed_ = in.value("speed", speed_);
        loop_ = in.value("loop", loop_);
        autoPlay_ = in.value("autoPlay", autoPlay_);
        playing_ = in.value("playing", playing_);
        finished_ = in.value("finished", finished_);
    }

    void AnimatorComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.String("Clip", clip_);
        builder.Float("Time Sec", timeSec_);
        builder.Float("Speed", speed_);
        builder.Bool("Loop", loop_);
        builder.Bool("Auto Play", autoPlay_);
        builder.Bool("Playing", playing_);
        builder.Bool("Finished", finished_);
        timeSec_ = ClampTime(timeSec_);
    }

    void AnimatorComponent::RenderImGui() {
#if defined(_DEBUG)
        char clipBuffer[256]{};
        std::strncpy(clipBuffer, clip_.c_str(), sizeof(clipBuffer) - 1);
        if (ImGui::InputText("Animation Clip", clipBuffer, sizeof(clipBuffer))) {
            SetClip(clipBuffer);
        }
        ImGui::DragFloat("Time Sec", &timeSec_, 0.01f, 0.0f, 100000.0f);
        ImGui::DragFloat("Speed", &speed_, 0.01f, -10.0f, 10.0f);
        ImGui::Checkbox("Loop", &loop_);
        ImGui::Checkbox("Auto Play", &autoPlay_);
        ImGui::Checkbox("Playing", &playing_);
        ImGui::Text("Finished: %s", finished_ ? "Yes" : "No");

        if (ImGui::Button("Play")) {
            Play(clip_, loop_, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause")) {
            Pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Resume")) {
            Resume();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            Stop();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Time")) {
            ResetTime();
        }
#endif
    }

    void AnimatorComponent::Play(std::string clip, bool loop, bool restart) {
        clip_ = std::move(clip);
        loop_ = loop;
        playing_ = true;
        finished_ = false;
        if (restart) {
            timeSec_ = 0.0f;
        }
    }

    void AnimatorComponent::Pause() {
        playing_ = false;
    }

    void AnimatorComponent::Resume() {
        if (!clip_.empty()) {
            playing_ = true;
            finished_ = false;
        }
    }

    void AnimatorComponent::Stop() {
        playing_ = false;
        finished_ = true;
        timeSec_ = 0.0f;
    }

    void AnimatorComponent::ResetTime() {
        timeSec_ = 0.0f;
        finished_ = false;
    }

    void AnimatorComponent::Advance(float deltaTimeSec, float clipDurationSec) {
        if (!autoPlay_ || !playing_ || clip_.empty()) {
            return;
        }

        timeSec_ += deltaTimeSec * speed_;
        if (clipDurationSec <= 0.0f) {
            timeSec_ = ClampTime(timeSec_);
            return;
        }

        if (loop_) {
            timeSec_ = std::fmod(timeSec_, clipDurationSec);
            if (timeSec_ < 0.0f) {
                timeSec_ += clipDurationSec;
            }
            finished_ = false;
            return;
        }

        if (speed_ >= 0.0f && timeSec_ >= clipDurationSec) {
            timeSec_ = clipDurationSec;
            playing_ = false;
            finished_ = true;
        } else if (speed_ < 0.0f && timeSec_ <= 0.0f) {
            timeSec_ = 0.0f;
            playing_ = false;
            finished_ = true;
        }
    }

    void AnimatorComponent::SetClip(std::string clip) {
        clip_ = std::move(clip);
        finished_ = false;
    }

    const std::string& AnimatorComponent::GetClip() const {
        return clip_;
    }

    void AnimatorComponent::SetTime(float timeSec) {
        timeSec_ = ClampTime(timeSec);
        finished_ = false;
    }

    float AnimatorComponent::GetTime() const {
        return timeSec_;
    }

    void AnimatorComponent::SetSpeed(float speed) {
        speed_ = speed;
    }

    float AnimatorComponent::GetSpeed() const {
        return speed_;
    }

    void AnimatorComponent::SetLoop(bool loop) {
        loop_ = loop;
    }

    bool AnimatorComponent::GetLoop() const {
        return loop_;
    }

    void AnimatorComponent::SetAutoPlay(bool autoPlay) {
        autoPlay_ = autoPlay;
    }

    bool AnimatorComponent::GetAutoPlay() const {
        return autoPlay_;
    }

    void AnimatorComponent::SetPlaying(bool playing) {
        playing_ = playing;
        if (playing_) {
            finished_ = false;
        }
    }

    bool AnimatorComponent::IsPlaying() const {
        return playing_;
    }

    bool AnimatorComponent::IsFinished() const {
        return finished_;
    }

    bool AnimatorComponent::IsFinished(float clipDurationSec) const {
        if (finished_) {
            return true;
        }
        if (clipDurationSec <= 0.0f || loop_) {
            return false;
        }
        return speed_ >= 0.0f ? timeSec_ >= clipDurationSec : timeSec_ <= 0.0f;
    }

} // namespace HIKARI
