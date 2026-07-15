#pragma once

#include <cstdint>

#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {

    enum class CameraTimelineKeyframeKind : uint8_t {
        None,
        Transform,
        Lens,
    };

    struct CameraTimelineKeyframeSelection {
        CameraTimelineKeyframeKind kind = CameraTimelineKeyframeKind::None;
        SEQUENCER::SequenceBindingId bindingId{};
        uint64_t keyframeId = 0;

        bool IsValid() const noexcept {
            return kind != CameraTimelineKeyframeKind::None &&
                bindingId.IsValid() && keyframeId != 0;
        }
    };

    struct CameraTimelineKeyframeLayout {
        float timelineLeft = 0.0f;
        float timelineRight = 0.0f;
        float transformTop = 0.0f;
        float transformBottom = 0.0f;
        float lensTop = 0.0f;
        float lensBottom = 0.0f;
        float scrollTimeSeconds = 0.0f;
        float pixelsPerSecond = 90.0f;
        float sequenceDurationSeconds = 0.0f;
        bool canvasHovered = false;
        bool editingAllowed = false;
        bool snapEnabled = true;
        float snapFramesPerSecond = 30.0f;
    };

    struct CameraTimelineKeyframeEditorResult {
        bool sequenceChanged = false;
        bool capturedLeftClick = false;
        bool selectionChanged = false;
    };

    class CameraTimelineKeyframeEditor {
    public:
        CameraTimelineKeyframeEditorResult Draw(
            CinematicSequence& sequence,
            const CameraTimelineKeyframeLayout& layout);

        void Reset();
        void CancelInteraction();
        void ClearSelection();
        bool HasSelection() const noexcept;
        CameraTimelineKeyframeSelection GetSelection() const noexcept;
        bool DeleteSelected(CinematicSequence& sequence);
        bool DrawSelectedKeyInspector(
            CinematicSequence& sequence,
            bool editingAllowed,
            bool snapEnabled,
            float snapFramesPerSecond);
        void SelectTransformKeyframe(
            SEQUENCER::SequenceBindingId bindingId,
            uint64_t keyframeId) noexcept;
        void SelectLensKeyframe(
            SEQUENCER::SequenceBindingId bindingId,
            uint64_t keyframeId) noexcept;

    private:
        CameraTimelineKeyframeKind selectedKind_ =
            CameraTimelineKeyframeKind::None;
        SEQUENCER::SequenceBindingId selectedBindingId_{};
        uint64_t selectedKeyframeId_ = 0;
        float dragStartMouseX_ = 0.0f;
        float dragStartTimeSeconds_ = 0.0f;
        bool dragging_ = false;
    };

} // namespace HIKARI::EDITOR
