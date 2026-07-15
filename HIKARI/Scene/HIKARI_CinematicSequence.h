#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    struct CinematicSequenceId {
        uint64_t value = 0;

        bool IsValid() const noexcept {
            return value != 0;
        }

        bool operator==(const CinematicSequenceId& rhs) const noexcept {
            return value == rhs.value;
        }
    };

    struct CinematicShotClip {
        uint64_t id = 0;
        SceneObjectId cameraObjectId{};
        float startTimeSeconds = 0.0f;
        float durationSeconds = 2.0f;
    };

    struct CameraCinematicSequence {
        CinematicSequenceId id{ 1 };
        std::string name{ "Main Sequence" };
        float durationSeconds = 10.0f;
        std::vector<CinematicShotClip> shots{};
    };

    struct SceneCinematicsSettings {
        CinematicSequenceId defaultSequenceId{ 1 };
        std::vector<CameraCinematicSequence> cameraSequences{
            CameraCinematicSequence{}
        };
    };

    struct CinematicSequenceEvaluation {
        uint64_t shotId = 0;
        SceneObjectId cameraObjectId{};
        float sequenceTimeSeconds = 0.0f;
        float localTimeSeconds = 0.0f;

        bool IsValid() const noexcept {
            return shotId != 0 && cameraObjectId.value != 0;
        }
    };

    constexpr float kMinCinematicShotDurationSeconds = 0.1f;
    constexpr float kMinCinematicSequenceDurationSeconds = 1.0f;
    constexpr float kMaxCinematicSequenceDurationSeconds = 3600.0f;

    void NormalizeCameraCinematicSequence(CameraCinematicSequence& sequence);
    void NormalizeSceneCinematicsSettings(SceneCinematicsSettings& settings);
    CinematicSequenceId AllocateCameraCinematicSequenceId(
        const SceneCinematicsSettings& settings);
    CameraCinematicSequence* FindCameraCinematicSequence(
        SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId) noexcept;
    const CameraCinematicSequence* FindCameraCinematicSequence(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId) noexcept;
    uint64_t AllocateCinematicShotId(const CameraCinematicSequence& sequence);
    float GetCameraCinematicSequenceContentEnd(
        const CameraCinematicSequence& sequence) noexcept;
    CinematicSequenceEvaluation EvaluateCameraCinematicSequence(
        const CameraCinematicSequence& sequence,
        float timeSeconds) noexcept;

} // namespace HIKARI
