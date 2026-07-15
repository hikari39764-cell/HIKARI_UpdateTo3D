#include "Scene/HIKARI_CinematicSequence.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace HIKARI {

    namespace {
        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }
    }

    void NormalizeCameraCinematicSequence(CameraCinematicSequence& sequence) {
        sequence.durationSeconds = std::clamp(
            FiniteOr(
                sequence.durationSeconds,
                kMinCinematicSequenceDurationSeconds),
            kMinCinematicSequenceDurationSeconds,
            kMaxCinematicSequenceDurationSeconds);

        sequence.shots.erase(
            std::remove_if(
                sequence.shots.begin(),
                sequence.shots.end(),
                [](const CinematicShotClip& shot) {
                    return shot.id == 0 || shot.cameraObjectId.value == 0;
                }),
            sequence.shots.end());

        for (CinematicShotClip& shot : sequence.shots) {
            shot.startTimeSeconds = std::clamp(
                FiniteOr(shot.startTimeSeconds, 0.0f),
                0.0f,
                kMaxCinematicSequenceDurationSeconds -
                    kMinCinematicShotDurationSeconds);
            shot.durationSeconds = std::clamp(
                FiniteOr(
                    shot.durationSeconds,
                    kMinCinematicShotDurationSeconds),
                kMinCinematicShotDurationSeconds,
                kMaxCinematicSequenceDurationSeconds -
                    shot.startTimeSeconds);
        }

        std::sort(
            sequence.shots.begin(),
            sequence.shots.end(),
            [](const CinematicShotClip& lhs, const CinematicShotClip& rhs) {
                if (lhs.startTimeSeconds != rhs.startTimeSeconds) {
                    return lhs.startTimeSeconds < rhs.startTimeSeconds;
                }
                return lhs.id < rhs.id;
            });

        sequence.durationSeconds = std::clamp(
            (std::max)(
                sequence.durationSeconds,
                GetCameraCinematicSequenceContentEnd(sequence)),
            kMinCinematicSequenceDurationSeconds,
            kMaxCinematicSequenceDurationSeconds);
    }

    void NormalizeSceneCinematicsSettings(
        SceneCinematicsSettings& settings) {

        if (settings.cameraSequences.empty()) {
            settings.cameraSequences.push_back(CameraCinematicSequence{});
        }

        std::unordered_set<uint64_t> usedIds{};
        CinematicSequenceId nextId =
            AllocateCameraCinematicSequenceId(settings);
        for (CameraCinematicSequence& sequence : settings.cameraSequences) {
            if (!sequence.id.IsValid() ||
                !usedIds.insert(sequence.id.value).second) {
                while (!usedIds.insert(nextId.value).second) {
                    ++nextId.value;
                    if (nextId.value == 0) {
                        nextId.value = 1;
                    }
                }
                sequence.id = nextId;
                ++nextId.value;
                if (nextId.value == 0) {
                    nextId.value = 1;
                }
            }
            if (sequence.name.empty()) {
                sequence.name = "Sequence " +
                    std::to_string(sequence.id.value);
            }
            NormalizeCameraCinematicSequence(sequence);
        }

        if (FindCameraCinematicSequence(
                settings,
                settings.defaultSequenceId) == nullptr) {
            settings.defaultSequenceId = settings.cameraSequences.front().id;
        }
    }

    CinematicSequenceId AllocateCameraCinematicSequenceId(
        const SceneCinematicsSettings& settings) {

        CinematicSequenceId nextId{ 1 };
        for (const CameraCinematicSequence& sequence :
                settings.cameraSequences) {
            if (sequence.id.value >= nextId.value) {
                if (sequence.id.value ==
                    (std::numeric_limits<uint64_t>::max)()) {
                    nextId.value = 1;
                    break;
                }
                nextId.value = sequence.id.value + 1;
            }
        }
        while (FindCameraCinematicSequence(settings, nextId) != nullptr) {
            ++nextId.value;
            if (nextId.value == 0) {
                nextId.value = 1;
            }
        }
        return nextId;
    }

    CameraCinematicSequence* FindCameraCinematicSequence(
        SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId) noexcept {

        const auto found = std::find_if(
            settings.cameraSequences.begin(),
            settings.cameraSequences.end(),
            [sequenceId](const CameraCinematicSequence& sequence) {
                return sequence.id == sequenceId;
            });
        return found != settings.cameraSequences.end() ? &*found : nullptr;
    }

    const CameraCinematicSequence* FindCameraCinematicSequence(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId) noexcept {

        const auto found = std::find_if(
            settings.cameraSequences.begin(),
            settings.cameraSequences.end(),
            [sequenceId](const CameraCinematicSequence& sequence) {
                return sequence.id == sequenceId;
            });
        return found != settings.cameraSequences.end() ? &*found : nullptr;
    }

    uint64_t AllocateCinematicShotId(
        const CameraCinematicSequence& sequence) {

        uint64_t nextId = 1;
        for (const CinematicShotClip& shot : sequence.shots) {
            if (shot.id >= nextId) {
                if (shot.id == (std::numeric_limits<uint64_t>::max)()) {
                    nextId = 1;
                    break;
                }
                nextId = shot.id + 1;
            }
        }

        while (std::any_of(
                sequence.shots.begin(),
                sequence.shots.end(),
                [nextId](const CinematicShotClip& shot) {
                    return shot.id == nextId;
                })) {
            ++nextId;
            if (nextId == 0) {
                nextId = 1;
            }
        }
        return nextId;
    }

    float GetCameraCinematicSequenceContentEnd(
        const CameraCinematicSequence& sequence) noexcept {

        float contentEnd = 0.0f;
        for (const CinematicShotClip& shot : sequence.shots) {
            const float start = FiniteOr(shot.startTimeSeconds, 0.0f);
            const float duration = FiniteOr(shot.durationSeconds, 0.0f);
            contentEnd = (std::max)(contentEnd, start + duration);
        }
        return contentEnd;
    }

    CinematicSequenceEvaluation EvaluateCameraCinematicSequence(
        const CameraCinematicSequence& sequence,
        float timeSeconds) noexcept {

        CinematicSequenceEvaluation result{};
        if (!std::isfinite(timeSeconds) || timeSeconds < 0.0f) {
            return result;
        }

        const CinematicShotClip* activeShot = nullptr;
        for (const CinematicShotClip& shot : sequence.shots) {
            if (shot.id == 0 || shot.cameraObjectId.value == 0 ||
                !std::isfinite(shot.startTimeSeconds) ||
                !std::isfinite(shot.durationSeconds) ||
                shot.durationSeconds < kMinCinematicShotDurationSeconds) {
                continue;
            }
            const float shotEnd = shot.startTimeSeconds + shot.durationSeconds;
            if (timeSeconds < shot.startTimeSeconds || timeSeconds >= shotEnd) {
                continue;
            }
            if (activeShot == nullptr ||
                shot.startTimeSeconds > activeShot->startTimeSeconds ||
                (shot.startTimeSeconds == activeShot->startTimeSeconds &&
                    shot.id > activeShot->id)) {
                activeShot = &shot;
            }
        }

        if (activeShot != nullptr) {
            result.shotId = activeShot->id;
            result.cameraObjectId = activeShot->cameraObjectId;
            result.sequenceTimeSeconds = timeSeconds;
            result.localTimeSeconds = timeSeconds - activeShot->startTimeSeconds;
        }
        return result;
    }

} // namespace HIKARI
