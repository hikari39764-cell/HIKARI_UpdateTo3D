#include "Editor/Workspaces/HIKARI_CameraTimelineKeyframeClipboard.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace HIKARI::EDITOR {

    bool CameraTimelineKeyframeClipboard::Copy(
        const CinematicSequence& sequence,
        const std::vector<CameraTimelineKeyframeSelection>& selections) {

        transforms_.clear();
        lenses_.clear();
        sourceStartTimeSeconds_ = 0.0f;

        float firstTime = (std::numeric_limits<float>::max)();
        for (const CameraTimelineKeyframeSelection& selection : selections) {
            if (selection.kind == CameraTimelineKeyframeKind::Transform) {
                const SEQUENCER::CameraTransformChannel* channel =
                    SEQUENCER::FindCameraTransformChannel(
                        sequence.cameraTransformTrack,
                        selection.bindingId);
                if (channel == nullptr) {
                    continue;
                }
                const auto found = std::find_if(
                    channel->keyframes.begin(),
                    channel->keyframes.end(),
                    [&selection](const auto& keyframe) {
                        return keyframe.id == selection.keyframeId;
                    });
                if (found != channel->keyframes.end()) {
                    firstTime = (std::min)(firstTime, found->timeSeconds);
                }
            } else if (selection.kind ==
                    CameraTimelineKeyframeKind::Lens) {
                const SEQUENCER::CameraLensChannel* channel =
                    SEQUENCER::FindCameraLensChannel(
                        sequence.cameraLensTrack,
                        selection.bindingId);
                if (channel == nullptr) {
                    continue;
                }
                const auto found = std::find_if(
                    channel->keyframes.begin(),
                    channel->keyframes.end(),
                    [&selection](const auto& keyframe) {
                        return keyframe.id == selection.keyframeId;
                    });
                if (found != channel->keyframes.end()) {
                    firstTime = (std::min)(firstTime, found->timeSeconds);
                }
            }
        }
        if (!std::isfinite(firstTime)) {
            return false;
        }
        sourceStartTimeSeconds_ = firstTime;

        for (const CameraTimelineKeyframeSelection& selection : selections) {
            const SEQUENCER::SequenceBinding* binding =
                SEQUENCER::FindSequenceBinding(
                    sequence.bindings,
                    selection.bindingId);
            if (binding == nullptr || binding->sceneObjectId.value == 0) {
                continue;
            }
            if (selection.kind == CameraTimelineKeyframeKind::Transform) {
                const SEQUENCER::CameraTransformChannel* channel =
                    SEQUENCER::FindCameraTransformChannel(
                        sequence.cameraTransformTrack,
                        selection.bindingId);
                if (channel == nullptr) {
                    continue;
                }
                const auto found = std::find_if(
                    channel->keyframes.begin(),
                    channel->keyframes.end(),
                    [&selection](const auto& keyframe) {
                        return keyframe.id == selection.keyframeId;
                    });
                if (found != channel->keyframes.end()) {
                    transforms_.push_back({
                        binding->sceneObjectId,
                        binding->name,
                        found->timeSeconds - firstTime,
                        found->position,
                        found->rotationEulerDeg,
                        found->interpolation
                    });
                }
            } else if (selection.kind ==
                    CameraTimelineKeyframeKind::Lens) {
                const SEQUENCER::CameraLensChannel* channel =
                    SEQUENCER::FindCameraLensChannel(
                        sequence.cameraLensTrack,
                        selection.bindingId);
                if (channel == nullptr) {
                    continue;
                }
                const auto found = std::find_if(
                    channel->keyframes.begin(),
                    channel->keyframes.end(),
                    [&selection](const auto& keyframe) {
                        return keyframe.id == selection.keyframeId;
                    });
                if (found != channel->keyframes.end()) {
                    lenses_.push_back({
                        binding->sceneObjectId,
                        binding->name,
                        found->timeSeconds - firstTime,
                        found->verticalFovDegrees,
                        found->nearClip,
                        found->farClip,
                        found->interpolation
                    });
                }
            }
        }
        return !Empty();
    }

    CameraTimelineClipboardPasteResult
        CameraTimelineKeyframeClipboard::Paste(
            CinematicSequence& sequence,
            float anchorTimeSeconds) const {

        CameraTimelineClipboardPasteResult result{};
        if (Empty() || !std::isfinite(anchorTimeSeconds)) {
            return result;
        }
        const float safeAnchor = std::clamp(
            anchorTimeSeconds,
            0.0f,
            kMaxCinematicSequenceDurationSeconds);
        float contentEnd = sequence.durationSeconds;

        for (const TransformPayload& payload : transforms_) {
            const SEQUENCER::SequenceBindingId bindingId =
                SEQUENCER::FindOrCreateSceneObjectBinding(
                    sequence.bindings,
                    payload.cameraObjectId,
                    payload.bindingName);
            const float timeSeconds = std::clamp(
                safeAnchor + payload.timeOffsetSeconds,
                0.0f,
                kMaxCinematicSequenceDurationSeconds);
            const uint64_t keyframeId =
                SEQUENCER::SetCameraTransformKeyframe(
                    sequence.cameraTransformTrack,
                    bindingId,
                    timeSeconds,
                    payload.position,
                    payload.rotationEulerDeg);
            if (SEQUENCER::CameraTransformKeyframe* keyframe =
                    SEQUENCER::FindCameraTransformKeyframe(
                        sequence.cameraTransformTrack,
                        bindingId,
                        keyframeId)) {
                keyframe->interpolation = payload.interpolation;
                result.selections.push_back({
                    CameraTimelineKeyframeKind::Transform,
                    bindingId,
                    keyframeId
                });
                result.sequenceChanged = true;
                contentEnd = (std::max)(contentEnd, timeSeconds);
            }
        }
        for (const LensPayload& payload : lenses_) {
            const SEQUENCER::SequenceBindingId bindingId =
                SEQUENCER::FindOrCreateSceneObjectBinding(
                    sequence.bindings,
                    payload.cameraObjectId,
                    payload.bindingName);
            const float timeSeconds = std::clamp(
                safeAnchor + payload.timeOffsetSeconds,
                0.0f,
                kMaxCinematicSequenceDurationSeconds);
            const uint64_t keyframeId = SEQUENCER::SetCameraLensKeyframe(
                sequence.cameraLensTrack,
                bindingId,
                timeSeconds,
                payload.verticalFovDegrees,
                payload.nearClip,
                payload.farClip);
            if (SEQUENCER::CameraLensKeyframe* keyframe =
                    SEQUENCER::FindCameraLensKeyframe(
                        sequence.cameraLensTrack,
                        bindingId,
                        keyframeId)) {
                keyframe->interpolation = payload.interpolation;
                result.selections.push_back({
                    CameraTimelineKeyframeKind::Lens,
                    bindingId,
                    keyframeId
                });
                result.sequenceChanged = true;
                contentEnd = (std::max)(contentEnd, timeSeconds);
            }
        }
        if (result.sequenceChanged) {
            sequence.durationSeconds = std::clamp(
                contentEnd,
                kMinCinematicSequenceDurationSeconds,
                kMaxCinematicSequenceDurationSeconds);
            NormalizeCinematicSequence(sequence);
        }
        return result;
    }

    bool CameraTimelineKeyframeClipboard::Empty() const noexcept {
        return transforms_.empty() && lenses_.empty();
    }

    size_t CameraTimelineKeyframeClipboard::Size() const noexcept {
        return transforms_.size() + lenses_.size();
    }

    float CameraTimelineKeyframeClipboard::GetSourceStartTimeSeconds()
        const noexcept {

        return sourceStartTimeSeconds_;
    }

} // namespace HIKARI::EDITOR
