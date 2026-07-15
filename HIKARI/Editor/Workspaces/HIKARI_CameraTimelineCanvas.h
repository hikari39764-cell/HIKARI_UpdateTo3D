#pragma once

#include <cstdint>

#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    struct CameraTimelineCanvasResult {
        bool sequenceChanged = false;
        bool playheadChanged = false;
        float playheadTimeSeconds = 0.0f;
    };

    class CameraTimelineCanvas {
    public:
        CameraTimelineCanvasResult Draw(
            const SceneDocument& document,
            CameraCinematicSequence& sequence,
            float playheadTimeSeconds,
            bool playing,
            bool editingAllowed);

        void Reset();
        void CancelInteraction();
        uint64_t GetSelectedShotId() const noexcept;
        void SetSelectedShotId(uint64_t shotId) noexcept;
        float GetPixelsPerSecond() const noexcept;
        void SetPixelsPerSecond(float pixelsPerSecond) noexcept;

    private:
        enum class DragMode : uint8_t {
            None,
            Move,
            ResizeLeft,
            ResizeRight,
        };

        float pixelsPerSecond_ = 90.0f;
        float scrollTimeSeconds_ = 0.0f;
        uint64_t selectedShotId_ = 0;
        uint64_t draggedShotId_ = 0;
        DragMode dragMode_ = DragMode::None;
        float dragStartMouseX_ = 0.0f;
        float dragStartShotTime_ = 0.0f;
        float dragStartShotDuration_ = 0.0f;
        float panStartMouseX_ = 0.0f;
        float panStartScrollTime_ = 0.0f;
        bool scrubbing_ = false;
        bool panning_ = false;
    };

} // namespace HIKARI::EDITOR
