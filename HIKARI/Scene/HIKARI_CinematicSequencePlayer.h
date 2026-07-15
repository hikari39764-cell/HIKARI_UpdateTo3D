#pragma once

#include <cstdint>

#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {

    enum class CinematicPlaybackState : uint8_t {
        Stopped,
        Playing,
        Paused,
    };

    struct CinematicPlaybackOptions {
        bool loop = false;
        float playbackRate = 1.0f;
    };

    class CinematicSequencePlayer {
    public:
        bool Bind(
            const SceneCinematicsSettings& settings,
            CinematicSequenceId sequenceId,
            float timeSeconds = 0.0f);
        bool Play(
            const SceneCinematicsSettings& settings,
            CinematicSequenceId sequenceId,
            float startTimeSeconds = 0.0f,
            const CinematicPlaybackOptions& options = {});
        bool Play();
        void Pause();
        bool Resume();
        void Stop();
        void Clear();
        bool Seek(
            const SceneCinematicsSettings& settings,
            float timeSeconds);

        CinematicSequenceEvaluation Tick(
            const SceneCinematicsSettings& settings,
            float deltaTime);
        CinematicSequenceEvaluation Evaluate(
            const SceneCinematicsSettings& settings) const noexcept;

        CinematicSequenceId GetSequenceId() const noexcept;
        CinematicPlaybackState GetState() const noexcept;
        float GetTimeSeconds() const noexcept;
        bool IsPlaying() const noexcept;
        bool IsPaused() const noexcept;
        bool CompletedThisTick() const noexcept;

    private:
        CinematicSequenceId sequenceId_{};
        CinematicPlaybackState state_ = CinematicPlaybackState::Stopped;
        CinematicPlaybackOptions options_{};
        float timeSeconds_ = 0.0f;
        bool completedThisTick_ = false;
    };

} // namespace HIKARI
