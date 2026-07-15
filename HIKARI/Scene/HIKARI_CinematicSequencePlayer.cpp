#include "Scene/HIKARI_CinematicSequencePlayer.h"

#include <algorithm>
#include <cmath>

namespace HIKARI {

    namespace {
        float ClampPlaybackRate(float playbackRate) noexcept {
            if (!std::isfinite(playbackRate)) {
                return 1.0f;
            }
            return std::clamp(playbackRate, 0.01f, 8.0f);
        }
    }

    bool CinematicSequencePlayer::Bind(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId,
        float timeSeconds) {

        const CameraCinematicSequence* sequence =
            FindCameraCinematicSequence(settings, sequenceId);
        if (sequence == nullptr) {
            Clear();
            return false;
        }

        sequenceId_ = sequenceId;
        timeSeconds_ = std::clamp(
            std::isfinite(timeSeconds) ? timeSeconds : 0.0f,
            0.0f,
            sequence->durationSeconds);
        state_ = CinematicPlaybackState::Stopped;
        options_ = {};
        completedThisTick_ = false;
        return true;
    }

    bool CinematicSequencePlayer::Play(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId,
        float startTimeSeconds,
        const CinematicPlaybackOptions& options) {

        if (!Bind(settings, sequenceId, startTimeSeconds)) {
            return false;
        }
        options_ = options;
        options_.playbackRate = ClampPlaybackRate(options.playbackRate);
        state_ = CinematicPlaybackState::Playing;
        return true;
    }

    bool CinematicSequencePlayer::Play() {
        if (!sequenceId_.IsValid()) {
            return false;
        }
        state_ = CinematicPlaybackState::Playing;
        completedThisTick_ = false;
        return true;
    }

    void CinematicSequencePlayer::Pause() {
        if (state_ == CinematicPlaybackState::Playing) {
            state_ = CinematicPlaybackState::Paused;
        }
    }

    bool CinematicSequencePlayer::Resume() {
        if (state_ != CinematicPlaybackState::Paused) {
            return false;
        }
        state_ = CinematicPlaybackState::Playing;
        completedThisTick_ = false;
        return true;
    }

    void CinematicSequencePlayer::Stop() {
        state_ = CinematicPlaybackState::Stopped;
        timeSeconds_ = 0.0f;
        completedThisTick_ = false;
    }

    void CinematicSequencePlayer::Clear() {
        sequenceId_ = {};
        state_ = CinematicPlaybackState::Stopped;
        options_ = {};
        timeSeconds_ = 0.0f;
        completedThisTick_ = false;
    }

    bool CinematicSequencePlayer::Seek(
        const SceneCinematicsSettings& settings,
        float timeSeconds) {

        const CameraCinematicSequence* sequence =
            FindCameraCinematicSequence(settings, sequenceId_);
        if (sequence == nullptr) {
            Clear();
            return false;
        }
        timeSeconds_ = std::clamp(
            std::isfinite(timeSeconds) ? timeSeconds : 0.0f,
            0.0f,
            sequence->durationSeconds);
        completedThisTick_ = false;
        return true;
    }

    CinematicSequenceEvaluation CinematicSequencePlayer::Tick(
        const SceneCinematicsSettings& settings,
        float deltaTime) {

        completedThisTick_ = false;
        const CameraCinematicSequence* sequence =
            FindCameraCinematicSequence(settings, sequenceId_);
        if (sequence == nullptr) {
            Clear();
            return {};
        }

        if (state_ == CinematicPlaybackState::Playing) {
            const float safeDelta = std::isfinite(deltaTime)
                ? std::clamp(deltaTime, 0.0f, 0.25f)
                : 0.0f;
            timeSeconds_ += safeDelta * options_.playbackRate;
            if (timeSeconds_ >= sequence->durationSeconds) {
                if (options_.loop) {
                    timeSeconds_ = std::fmod(
                        timeSeconds_,
                        sequence->durationSeconds);
                } else {
                    timeSeconds_ = sequence->durationSeconds;
                    state_ = CinematicPlaybackState::Stopped;
                    completedThisTick_ = true;
                }
            }
        }
        return Evaluate(settings);
    }

    CinematicSequenceEvaluation CinematicSequencePlayer::Evaluate(
        const SceneCinematicsSettings& settings) const noexcept {

        const CameraCinematicSequence* sequence =
            FindCameraCinematicSequence(settings, sequenceId_);
        return sequence != nullptr
            ? EvaluateCameraCinematicSequence(*sequence, timeSeconds_)
            : CinematicSequenceEvaluation{};
    }

    CinematicSequenceId CinematicSequencePlayer::GetSequenceId() const noexcept {
        return sequenceId_;
    }

    CinematicPlaybackState CinematicSequencePlayer::GetState() const noexcept {
        return state_;
    }

    float CinematicSequencePlayer::GetTimeSeconds() const noexcept {
        return timeSeconds_;
    }

    bool CinematicSequencePlayer::IsPlaying() const noexcept {
        return state_ == CinematicPlaybackState::Playing;
    }

    bool CinematicSequencePlayer::IsPaused() const noexcept {
        return state_ == CinematicPlaybackState::Paused;
    }

    bool CinematicSequencePlayer::CompletedThisTick() const noexcept {
        return completedThisTick_;
    }

} // namespace HIKARI
