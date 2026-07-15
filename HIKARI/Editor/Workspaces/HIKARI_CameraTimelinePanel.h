#pragma once

#include <array>
#include <cstdint>

#include "Editor/Workspaces/HIKARI_CameraTimelineCanvas.h"
#include "Scene/HIKARI_CinematicSequencePlayer.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    struct CameraTimelinePanelResult {
        bool documentChanged = false;
        bool previewEnabled = false;
        CinematicSequenceId sequenceId{};
        CinematicSequenceEvaluation evaluation{};
    };

    class CameraTimelinePanel {
    public:
        CameraTimelinePanelResult Draw(
            SceneDocument& document,
            SceneObjectId selectedCameraObjectId,
            float deltaTime,
            bool previewAllowed);

        void ResetForScene();
        void PausePlayback();
        void SuspendPreview();

    private:
        bool EnsureActiveSequence(SceneCinematicsSettings& settings);
        bool SetActiveSequence(
            SceneCinematicsSettings& settings,
            CinematicSequenceId sequenceId);
        void SyncNameBuffer(const CameraCinematicSequence& sequence);

        CinematicSequencePlayer player_{};
        CameraTimelineCanvas canvas_{};
        CinematicSequenceId activeSequenceId_{};
        std::array<char, 128> sequenceNameBuffer_{};
        bool previewEnabled_ = true;
    };

} // namespace HIKARI::EDITOR
