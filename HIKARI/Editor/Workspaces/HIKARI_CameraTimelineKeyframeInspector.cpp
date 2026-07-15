#include "Editor/Workspaces/HIKARI_CameraTimelineKeyframeInspector.h"

#include <cmath>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        const char* InterpolationLabel(
            SEQUENCER::SequenceInterpolationMode mode) noexcept {

            switch (mode) {
            case SEQUENCER::SequenceInterpolationMode::Hold:
                return "Hold";
            case SEQUENCER::SequenceInterpolationMode::Linear:
                return "Linear";
            default:
                return "Smooth";
            }
        }

        bool DrawInterpolation(
            SEQUENCER::SequenceInterpolationMode& mode) {

            bool changed = false;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(92.0f);
            if (ImGui::BeginCombo(
                    "##CameraKeyInterpolation",
                    InterpolationLabel(mode))) {
                const auto option = [&changed, &mode](
                    const char* label,
                    SEQUENCER::SequenceInterpolationMode optionMode) {

                    if (ImGui::Selectable(label, mode == optionMode)) {
                        mode = optionMode;
                        changed = true;
                    }
                };
                option("Hold", SEQUENCER::SequenceInterpolationMode::Hold);
                option(
                    "Linear",
                    SEQUENCER::SequenceInterpolationMode::Linear);
                option(
                    "Smooth",
                    SEQUENCER::SequenceInterpolationMode::Smooth);
                ImGui::EndCombo();
            }
            return changed;
        }

        float SnapTime(
            float timeSeconds,
            bool snapEnabled,
            float snapFramesPerSecond) noexcept {

            if (!snapEnabled || !std::isfinite(snapFramesPerSecond) ||
                snapFramesPerSecond <= 0.0f) {
                return timeSeconds;
            }
            return std::round(timeSeconds * snapFramesPerSecond) /
                snapFramesPerSecond;
        }
#endif
    }

    bool DrawCameraTimelineKeyframeInspector(
        CinematicSequence& sequence,
        const CameraTimelineKeyframeSelection& selection,
        bool editingAllowed,
        bool snapEnabled,
        float snapFramesPerSecond) {

        bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
        if (!selection.IsValid()) {
            return false;
        }
        if (!editingAllowed) {
            ImGui::BeginDisabled();
        }

        if (selection.kind == CameraTimelineKeyframeKind::Transform) {
            SEQUENCER::CameraTransformKeyframe* keyframe =
                SEQUENCER::FindCameraTransformKeyframe(
                    sequence.cameraTransformTrack,
                    selection.bindingId,
                    selection.keyframeId);
            if (keyframe != nullptr) {
                ImGui::TextDisabled("Transform Key");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(82.0f);
                float editedTime = keyframe->timeSeconds;
                if (ImGui::DragFloat(
                    "##CameraTransformKeyTime",
                    &editedTime,
                    0.01f,
                    0.0f,
                    sequence.durationSeconds,
                    "%.3f s")) {
                    changed |= SEQUENCER::MoveCameraTransformKeyframe(
                        sequence.cameraTransformTrack,
                        selection.bindingId,
                        selection.keyframeId,
                        SnapTime(
                            editedTime,
                            snapEnabled,
                            snapFramesPerSecond));
                    keyframe = SEQUENCER::FindCameraTransformKeyframe(
                        sequence.cameraTransformTrack,
                        selection.bindingId,
                        selection.keyframeId);
                }
                if (keyframe != nullptr) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(230.0f);
                    changed |= ImGui::DragFloat3(
                        "##CameraTransformKeyPosition",
                        &keyframe->position.x,
                        0.01f,
                        0.0f,
                        0.0f,
                        "P %.2f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(230.0f);
                    changed |= ImGui::DragFloat3(
                        "##CameraTransformKeyRotation",
                        &keyframe->rotationEulerDeg.x,
                        0.1f,
                        0.0f,
                        0.0f,
                        "R %.1f");
                    changed |= DrawInterpolation(keyframe->interpolation);
                }
            }
        } else if (selection.kind == CameraTimelineKeyframeKind::Lens) {
            SEQUENCER::CameraLensKeyframe* keyframe =
                SEQUENCER::FindCameraLensKeyframe(
                    sequence.cameraLensTrack,
                    selection.bindingId,
                    selection.keyframeId);
            if (keyframe != nullptr) {
                ImGui::TextDisabled("Lens Key");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(82.0f);
                float editedTime = keyframe->timeSeconds;
                if (ImGui::DragFloat(
                    "##CameraLensKeyTime",
                    &editedTime,
                    0.01f,
                    0.0f,
                    sequence.durationSeconds,
                    "%.3f s")) {
                    changed |= SEQUENCER::MoveCameraLensKeyframe(
                        sequence.cameraLensTrack,
                        selection.bindingId,
                        selection.keyframeId,
                        SnapTime(
                            editedTime,
                            snapEnabled,
                            snapFramesPerSecond));
                    keyframe = SEQUENCER::FindCameraLensKeyframe(
                        sequence.cameraLensTrack,
                        selection.bindingId,
                        selection.keyframeId);
                }
                if (keyframe != nullptr) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(92.0f);
                    changed |= ImGui::DragFloat(
                        "##CameraLensKeyFov",
                        &keyframe->verticalFovDegrees,
                        0.1f,
                        1.0f,
                        179.0f,
                        "FOV %.1f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(92.0f);
                    changed |= ImGui::DragFloat(
                        "##CameraLensKeyNear",
                        &keyframe->nearClip,
                        0.001f,
                        0.001f,
                        keyframe->farClip,
                        "Near %.3f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100.0f);
                    changed |= ImGui::DragFloat(
                        "##CameraLensKeyFar",
                        &keyframe->farClip,
                        0.1f,
                        keyframe->nearClip + 0.001f,
                        100000.0f,
                        "Far %.1f");
                    changed |= DrawInterpolation(keyframe->interpolation);
                }
            }
        }

        if (changed) {
            NormalizeCinematicSequence(sequence);
        }
        if (!editingAllowed) {
            ImGui::EndDisabled();
        }
#else
        (void)sequence;
        (void)selection;
        (void)editingAllowed;
        (void)snapEnabled;
        (void)snapFramesPerSecond;
#endif
        return changed;
    }

} // namespace HIKARI::EDITOR
