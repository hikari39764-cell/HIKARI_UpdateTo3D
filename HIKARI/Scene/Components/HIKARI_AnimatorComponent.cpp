#include "HIKARI_AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        float ClampTime(float value) {
            return (std::max)(0.0f, value);
        }

        const ModelAsset* FindOwnerModelAsset(const AnimatorComponent& animator) {
            const GameObject* owner = animator.GetOwner();
            if (owner == nullptr) {
                return nullptr;
            }
            const ModelComponent* model = owner->GetComponent<ModelComponent>();
            return model != nullptr ? model->GetModelAsset() : nullptr;
        }

#if defined(_DEBUG)
        const AnimationClip* FindClip(const ModelAsset* asset, const std::string& clipName) {
            return asset != nullptr ? asset->FindAnimationClip(clipName) : nullptr;
        }
#endif
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
        playing_ = in.value("playing", autoPlay_);
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
        const ModelAsset* modelAsset = FindOwnerModelAsset(*this);
        const AnimationClip* currentClip = FindClip(modelAsset, clip_);
        int currentIndex = -1;
        if (modelAsset != nullptr) {
            for (size_t i = 0; i < modelAsset->animations.size(); ++i) {
                if (modelAsset->animations[i].name == clip_) {
                    currentIndex = static_cast<int>(i);
                    currentClip = &modelAsset->animations[i];
                    break;
                }
            }
        }

        const char* preview = clip_.empty() ? "<none>" : clip_.c_str();
        if (modelAsset != nullptr && !modelAsset->animations.empty()) {
            if (ImGui::BeginCombo("Animation Clip", preview)) {
                for (size_t i = 0; i < modelAsset->animations.size(); ++i) {
                    const AnimationClip& clip = modelAsset->animations[i];
                    const bool selected = static_cast<int>(i) == currentIndex;
                    std::string label = clip.name + " (" + std::to_string(clip.durationSec) + "s)";
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        Play(clip.name, loop_, true);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextUnformatted("Animation Clip: <no clips on current model>");
        }

        char clipBuffer[256]{};
        std::strncpy(clipBuffer, clip_.c_str(), sizeof(clipBuffer) - 1);
        if (ImGui::InputText("Manual Clip Name", clipBuffer, sizeof(clipBuffer))) {
            SetClip(clipBuffer);
        }
        ImGui::DragFloat("Time Sec", &timeSec_, 0.01f, 0.0f, 100000.0f);
        ImGui::DragFloat("Speed", &speed_, 0.01f, -10.0f, 10.0f);
        ImGui::Checkbox("Loop", &loop_);
        ImGui::Checkbox("Auto Play", &autoPlay_);
        ImGui::Checkbox("Playing", &playing_);
        ImGui::Text("Finished: %s", finished_ ? "Yes" : "No");
        if (currentClip != nullptr) {
            const float duration = currentClip->durationSec;
            const float normalized = duration > 0.0f ? std::clamp(timeSec_ / duration, 0.0f, 1.0f) : 0.0f;
            ImGui::Text("Current Clip Duration: %.2f", duration);
            ImGui::Text("Progress: %.2f / %.2f", timeSec_, duration);
            ImGui::Text("Normalized Time: %.3f", normalized);
            ImGui::ProgressBar(normalized);
        } else if (!clip_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Clip not found in current model");
        }

        if (ImGui::Button("Play")) {
            PlayCurrent(true);
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
        ImGui::SameLine();
        if (ImGui::Button("Play Once")) {
            PlayOnce(clip_, true);
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

    void AnimatorComponent::PlayCurrent(bool restart) {
        Play(clip_, loop_, restart);
    }

    void AnimatorComponent::PlayOnce(std::string clip, bool restart) {
        Play(std::move(clip), false, restart);
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
        if (!playing_ || clip_.empty()) {
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
