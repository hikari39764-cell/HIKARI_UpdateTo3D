#include "Editor/Workspaces/HIKARI_CameraTimelinePanel.h"

#include <algorithm>
#include <cstdio>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
        constexpr float kDefaultShotDurationSeconds = 2.0f;

        CinematicShotClip* FindShot(
            CameraCinematicSequence& sequence,
            uint64_t shotId) {

            const auto found = std::find_if(
                sequence.shots.begin(),
                sequence.shots.end(),
                [shotId](const CinematicShotClip& shot) {
                    return shot.id == shotId;
                });
            return found != sequence.shots.end() ? &*found : nullptr;
        }
    }

    CameraTimelinePanelResult CameraTimelinePanel::Draw(
        SceneDocument& document,
        SceneObjectId selectedCameraObjectId,
        float deltaTime,
        bool previewAllowed) {

        CameraTimelinePanelResult result{};
        SceneCinematicsSettings& settings = document.cinematics;
        NormalizeSceneCinematicsSettings(settings);
        if (!EnsureActiveSequence(settings)) {
            return result;
        }

#if defined(HIKARI_WITH_EDITOR)
        if (!previewAllowed) {
            PausePlayback();
        } else {
            (void)player_.Tick(settings, deltaTime);
        }

        CameraCinematicSequence* sequence =
            FindCameraCinematicSequence(settings, activeSequenceId_);
        if (sequence == nullptr) {
            return result;
        }

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo(
                "##CameraSequenceSelector",
                sequence->name.c_str())) {
            for (const CameraCinematicSequence& candidate :
                    settings.cameraSequences) {
                const bool selected = candidate.id == activeSequenceId_;
                std::string label = candidate.name;
                if (candidate.id == settings.defaultSequenceId) {
                    label += "  [Default]";
                }
                if (ImGui::Selectable(label.c_str(), selected)) {
                    (void)SetActiveSequence(settings, candidate.id);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (!previewAllowed) {
            ImGui::BeginDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("New Sequence")) {
            CameraCinematicSequence newSequence{};
            newSequence.id = AllocateCameraCinematicSequenceId(settings);
            newSequence.name = "Sequence " +
                std::to_string(newSequence.id.value);
            settings.cameraSequences.push_back(newSequence);
            (void)SetActiveSequence(settings, newSequence.id);
            result.documentChanged = true;
        }

        ImGui::SameLine();
        const bool canDeleteSequence = settings.cameraSequences.size() > 1;
        if (!canDeleteSequence) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete Sequence")) {
            settings.cameraSequences.erase(
                std::remove_if(
                    settings.cameraSequences.begin(),
                    settings.cameraSequences.end(),
                    [this](const CameraCinematicSequence& candidate) {
                        return candidate.id == activeSequenceId_;
                    }),
                settings.cameraSequences.end());
            NormalizeSceneCinematicsSettings(settings);
            (void)SetActiveSequence(
                settings,
                settings.defaultSequenceId);
            result.documentChanged = true;
        }
        if (!canDeleteSequence) {
            ImGui::EndDisabled();
        }

        sequence = FindCameraCinematicSequence(settings, activeSequenceId_);
        if (sequence == nullptr) {
            if (!previewAllowed) {
                ImGui::EndDisabled();
            }
            return result;
        }

        ImGui::SameLine();
        const bool isDefaultSequence =
            sequence->id == settings.defaultSequenceId;
        if (isDefaultSequence) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Set Default")) {
            settings.defaultSequenceId = sequence->id;
            result.documentChanged = true;
        }
        if (isDefaultSequence) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::InputText(
                "##CameraSequenceName",
                sequenceNameBuffer_.data(),
                sequenceNameBuffer_.size())) {
            if (sequenceNameBuffer_[0] != '\0') {
                sequence->name = sequenceNameBuffer_.data();
                result.documentChanged = true;
            }
        }
        if (!previewAllowed) {
            ImGui::EndDisabled();
        }

        ImGui::Separator();

        const bool canAddShot =
            previewAllowed && selectedCameraObjectId.value != 0;
        if (!canAddShot) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Add Shot")) {
            CinematicShotClip shot{};
            shot.id = AllocateCinematicShotId(*sequence);
            shot.cameraObjectId = selectedCameraObjectId;
            shot.startTimeSeconds = player_.GetTimeSeconds();
            shot.durationSeconds = kDefaultShotDurationSeconds;
            sequence->shots.push_back(shot);
            sequence->durationSeconds = (std::max)(
                sequence->durationSeconds,
                shot.startTimeSeconds + shot.durationSeconds);
            NormalizeCameraCinematicSequence(*sequence);
            canvas_.SetSelectedShotId(shot.id);
            previewEnabled_ = true;
            result.documentChanged = true;
        }
        if (!canAddShot) {
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                canAddShot
                    ? "Add the selected camera at the playhead"
                    : "Select a Camera object to add a shot");
        }

        ImGui::SameLine();
        if (!previewAllowed) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(player_.IsPlaying() ? "Pause" : "Play")) {
            if (player_.IsPlaying()) {
                player_.Pause();
            } else {
                if (player_.GetTimeSeconds() >= sequence->durationSeconds) {
                    (void)player_.Seek(settings, 0.0f);
                }
                (void)player_.Play();
            }
            previewEnabled_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            player_.Stop();
            previewEnabled_ = true;
        }
        if (!previewAllowed) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(82.0f);
        float currentTimeSeconds = player_.GetTimeSeconds();
        if (ImGui::DragFloat(
                "##CameraTimelineTime",
                &currentTimeSeconds,
                0.02f,
                0.0f,
                sequence->durationSeconds,
                "%.2f s")) {
            player_.Pause();
            (void)player_.Seek(settings, currentTimeSeconds);
            previewEnabled_ = true;
        }

        ImGui::SameLine();
        bool previewEnabled = previewEnabled_;
        if (ImGui::Checkbox("Preview", &previewEnabled)) {
            previewEnabled_ = previewEnabled;
        }

        ImGui::SameLine();
        const bool canDeleteSelectedShot =
            previewAllowed &&
            FindShot(*sequence, canvas_.GetSelectedShotId()) != nullptr;
        if (!canDeleteSelectedShot) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete Shot")) {
            const uint64_t selectedShotId = canvas_.GetSelectedShotId();
            sequence->shots.erase(
                std::remove_if(
                    sequence->shots.begin(),
                    sequence->shots.end(),
                    [selectedShotId](const CinematicShotClip& shot) {
                        return shot.id == selectedShotId;
                    }),
                sequence->shots.end());
            canvas_.SetSelectedShotId(0);
            result.documentChanged = true;
        }
        if (!canDeleteSelectedShot) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Duration");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(78.0f);
        float sequenceDuration = sequence->durationSeconds;
        if (!previewAllowed) {
            ImGui::BeginDisabled();
        }
        if (ImGui::DragFloat(
                "##CameraTimelineDuration",
                &sequenceDuration,
                0.1f,
                kMinCinematicSequenceDurationSeconds,
                kMaxCinematicSequenceDurationSeconds,
                "%.1f s")) {
            sequence->durationSeconds = sequenceDuration;
            NormalizeCameraCinematicSequence(*sequence);
            (void)player_.Seek(settings, player_.GetTimeSeconds());
            result.documentChanged = true;
        }
        if (!previewAllowed) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Zoom");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        float pixelsPerSecond = canvas_.GetPixelsPerSecond();
        if (ImGui::SliderFloat(
                "##CameraTimelineZoom",
                &pixelsPerSecond,
                24.0f,
                220.0f,
                "%.0f px/s")) {
            canvas_.SetPixelsPerSecond(pixelsPerSecond);
        }

        ImGui::Separator();
        const CameraTimelineCanvasResult canvasResult = canvas_.Draw(
            document,
            *sequence,
            player_.GetTimeSeconds(),
            player_.IsPlaying(),
            previewAllowed);
        if (canvasResult.playheadChanged) {
            player_.Pause();
            (void)player_.Seek(
                settings,
                canvasResult.playheadTimeSeconds);
            previewEnabled_ = true;
        }
        if (canvasResult.sequenceChanged) {
            NormalizeCameraCinematicSequence(*sequence);
            (void)player_.Seek(settings, player_.GetTimeSeconds());
            result.documentChanged = true;
        }

        result.previewEnabled = previewAllowed && previewEnabled_;
        result.sequenceId = activeSequenceId_;
        if (result.previewEnabled) {
            result.evaluation = player_.Evaluate(settings);
        }
#else
        (void)selectedCameraObjectId;
        (void)deltaTime;
        (void)previewAllowed;
#endif
        return result;
    }

    void CameraTimelinePanel::ResetForScene() {
        player_.Clear();
        canvas_.Reset();
        activeSequenceId_ = {};
        sequenceNameBuffer_.fill('\0');
        previewEnabled_ = true;
    }

    void CameraTimelinePanel::PausePlayback() {
        player_.Pause();
        canvas_.CancelInteraction();
    }

    void CameraTimelinePanel::SuspendPreview() {
        PausePlayback();
        previewEnabled_ = false;
    }

    bool CameraTimelinePanel::EnsureActiveSequence(
        SceneCinematicsSettings& settings) {

        CameraCinematicSequence* sequence =
            FindCameraCinematicSequence(settings, activeSequenceId_);
        if (sequence == nullptr) {
            return SetActiveSequence(settings, settings.defaultSequenceId);
        }
        if (!(player_.GetSequenceId() == activeSequenceId_)) {
            return player_.Bind(settings, activeSequenceId_, 0.0f);
        }
        return true;
    }

    bool CameraTimelinePanel::SetActiveSequence(
        SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId) {

        CameraCinematicSequence* sequence =
            FindCameraCinematicSequence(settings, sequenceId);
        if (sequence == nullptr ||
            !player_.Bind(settings, sequenceId, 0.0f)) {
            return false;
        }
        activeSequenceId_ = sequenceId;
        canvas_.Reset();
        SyncNameBuffer(*sequence);
        previewEnabled_ = true;
        return true;
    }

    void CameraTimelinePanel::SyncNameBuffer(
        const CameraCinematicSequence& sequence) {

        sequenceNameBuffer_.fill('\0');
        std::snprintf(
            sequenceNameBuffer_.data(),
            sequenceNameBuffer_.size(),
            "%s",
            sequence.name.c_str());
    }

} // namespace HIKARI::EDITOR
