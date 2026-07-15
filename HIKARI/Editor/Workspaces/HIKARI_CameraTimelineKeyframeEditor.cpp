#include "Editor/Workspaces/HIKARI_CameraTimelineKeyframeEditor.h"

#include "Editor/Workspaces/HIKARI_CameraTimelineKeyframeInspector.h"

#include <algorithm>
#include <cmath>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr float kKeyframeRadius = 6.0f;
        constexpr float kKeyframeHitRadius = 9.0f;

        ImU32 KeyframeColor(
            SEQUENCER::SequenceBindingId bindingId,
            bool selected) {

            const uint32_t hash = static_cast<uint32_t>(
                bindingId.value ^ (bindingId.value >> 32u));
            const float hue = static_cast<float>(hash % 360u) / 360.0f;
            ImVec4 color{};
            ImGui::ColorConvertHSVtoRGB(
                hue,
                selected ? 0.52f : 0.45f,
                selected ? 1.0f : 0.88f,
                color.x,
                color.y,
                color.z);
            color.w = 1.0f;
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        void DrawDiamond(
            ImDrawList& drawList,
            float x,
            float y,
            ImU32 fill,
            bool selected) {

            const ImVec2 points[4] = {
                { x, y - kKeyframeRadius },
                { x + kKeyframeRadius, y },
                { x, y + kKeyframeRadius },
                { x - kKeyframeRadius, y }
            };
            drawList.AddConvexPolyFilled(points, 4, fill);
            drawList.AddPolyline(
                points,
                4,
                selected
                    ? IM_COL32(255, 230, 142, 255)
                    : IM_COL32(220, 226, 236, 230),
                ImDrawFlags_Closed,
                selected ? 2.0f : 1.0f);
        }
#endif

    }

    CameraTimelineKeyframeEditorResult CameraTimelineKeyframeEditor::Draw(
        CinematicSequence& sequence,
        const CameraTimelineKeyframeLayout& layout) {

        CameraTimelineKeyframeEditorResult result{};
#if defined(HIKARI_WITH_EDITOR)
        struct HoveredKeyframe {
            CameraTimelineKeyframeKind kind =
                CameraTimelineKeyframeKind::None;
            SEQUENCER::SequenceBindingId bindingId{};
            uint64_t keyframeId = 0;
            float timeSeconds = 0.0f;
        } hovered{};

        ImDrawList& drawList = *ImGui::GetWindowDrawList();
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        const auto drawKeyframe = [&](
            CameraTimelineKeyframeKind kind,
            SEQUENCER::SequenceBindingId bindingId,
            uint64_t keyframeId,
            float timeSeconds,
            float laneTop,
            float laneBottom) {

            const float x = layout.timelineLeft +
                (timeSeconds - layout.scrollTimeSeconds) *
                    layout.pixelsPerSecond;
            if (x < layout.timelineLeft || x > layout.timelineRight) {
                return;
            }
            const float y = (laneTop + laneBottom) * 0.5f;
            const bool selected = selectedKind_ == kind &&
                selectedBindingId_ == bindingId &&
                selectedKeyframeId_ == keyframeId;
            DrawDiamond(
                drawList,
                x,
                y,
                KeyframeColor(bindingId, selected),
                selected);
            const float deltaX = mousePosition.x - x;
            const float deltaY = mousePosition.y - y;
            if (deltaX * deltaX + deltaY * deltaY <=
                    kKeyframeHitRadius * kKeyframeHitRadius) {
                hovered = { kind, bindingId, keyframeId, timeSeconds };
            }
        };

        for (const SEQUENCER::CameraTransformChannel& channel :
                sequence.cameraTransformTrack.channels) {
            for (const SEQUENCER::CameraTransformKeyframe& keyframe :
                    channel.keyframes) {
                drawKeyframe(
                    CameraTimelineKeyframeKind::Transform,
                    channel.cameraBindingId,
                    keyframe.id,
                    keyframe.timeSeconds,
                    layout.transformTop,
                    layout.transformBottom);
            }
        }
        for (const SEQUENCER::CameraLensChannel& channel :
                sequence.cameraLensTrack.channels) {
            for (const SEQUENCER::CameraLensKeyframe& keyframe :
                    channel.keyframes) {
                drawKeyframe(
                    CameraTimelineKeyframeKind::Lens,
                    channel.cameraBindingId,
                    keyframe.id,
                    keyframe.timeSeconds,
                    layout.lensTop,
                    layout.lensBottom);
            }
        }

        if (layout.canvasHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            hovered.kind != CameraTimelineKeyframeKind::None) {
            const bool changedSelection = selectedKind_ != hovered.kind ||
                !(selectedBindingId_ == hovered.bindingId) ||
                selectedKeyframeId_ != hovered.keyframeId;
            selectedKind_ = hovered.kind;
            selectedBindingId_ = hovered.bindingId;
            selectedKeyframeId_ = hovered.keyframeId;
            result.selectionChanged = changedSelection;
            result.capturedLeftClick = true;
            if (layout.editingAllowed) {
                dragging_ = true;
                dragStartMouseX_ = mousePosition.x;
                dragStartTimeSeconds_ = hovered.timeSeconds;
            }
        }

        if (dragging_) {
            if (layout.editingAllowed &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float newTime = std::clamp(
                    dragStartTimeSeconds_ +
                        (mousePosition.x - dragStartMouseX_) /
                            layout.pixelsPerSecond,
                    0.0f,
                    layout.sequenceDurationSeconds);
                const float snappedTime = layout.snapEnabled &&
                        layout.snapFramesPerSecond > 0.0f
                    ? std::round(
                        newTime * layout.snapFramesPerSecond) /
                        layout.snapFramesPerSecond
                    : newTime;
                if (selectedKind_ ==
                        CameraTimelineKeyframeKind::Transform) {
                    result.sequenceChanged |=
                        SEQUENCER::MoveCameraTransformKeyframe(
                            sequence.cameraTransformTrack,
                            selectedBindingId_,
                            selectedKeyframeId_,
                            snappedTime);
                } else if (selectedKind_ ==
                        CameraTimelineKeyframeKind::Lens) {
                    result.sequenceChanged |=
                        SEQUENCER::MoveCameraLensKeyframe(
                            sequence.cameraLensTrack,
                            selectedBindingId_,
                            selectedKeyframeId_,
                            snappedTime);
                }
            } else {
                dragging_ = false;
                SEQUENCER::NormalizeCameraTransformTrack(
                    sequence.cameraTransformTrack);
                SEQUENCER::NormalizeCameraLensTrack(
                    sequence.cameraLensTrack);
            }
        }

        if (layout.canvasHovered &&
            hovered.kind != CameraTimelineKeyframeKind::None) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::SetTooltip(
                "%s Key\nTime %.2f s",
                hovered.kind == CameraTimelineKeyframeKind::Transform
                    ? "Transform"
                    : "Lens",
                hovered.timeSeconds);
        }
#else
        (void)sequence;
        (void)layout;
#endif
        return result;
    }

    void CameraTimelineKeyframeEditor::Reset() {
        ClearSelection();
        CancelInteraction();
    }

    void CameraTimelineKeyframeEditor::CancelInteraction() {
        dragging_ = false;
    }

    void CameraTimelineKeyframeEditor::ClearSelection() {
        selectedKind_ = CameraTimelineKeyframeKind::None;
        selectedBindingId_ = {};
        selectedKeyframeId_ = 0;
    }

    bool CameraTimelineKeyframeEditor::HasSelection() const noexcept {
        return selectedKind_ != CameraTimelineKeyframeKind::None &&
            selectedBindingId_.IsValid() && selectedKeyframeId_ != 0;
    }

    CameraTimelineKeyframeSelection
        CameraTimelineKeyframeEditor::GetSelection() const noexcept {

        return {
            selectedKind_,
            selectedBindingId_,
            selectedKeyframeId_
        };
    }

    bool CameraTimelineKeyframeEditor::DeleteSelected(
        CinematicSequence& sequence) {

        if (!HasSelection()) {
            return false;
        }
        bool removed = false;
        if (selectedKind_ == CameraTimelineKeyframeKind::Transform) {
            if (SEQUENCER::CameraTransformChannel* channel =
                    SEQUENCER::FindCameraTransformChannel(
                        sequence.cameraTransformTrack,
                        selectedBindingId_)) {
                const size_t oldSize = channel->keyframes.size();
                channel->keyframes.erase(
                    std::remove_if(
                        channel->keyframes.begin(),
                        channel->keyframes.end(),
                        [this](const auto& keyframe) {
                            return keyframe.id == selectedKeyframeId_;
                        }),
                    channel->keyframes.end());
                removed = channel->keyframes.size() != oldSize;
            }
            SEQUENCER::NormalizeCameraTransformTrack(
                sequence.cameraTransformTrack);
        } else if (selectedKind_ == CameraTimelineKeyframeKind::Lens) {
            if (SEQUENCER::CameraLensChannel* channel =
                    SEQUENCER::FindCameraLensChannel(
                        sequence.cameraLensTrack,
                        selectedBindingId_)) {
                const size_t oldSize = channel->keyframes.size();
                channel->keyframes.erase(
                    std::remove_if(
                        channel->keyframes.begin(),
                        channel->keyframes.end(),
                        [this](const auto& keyframe) {
                            return keyframe.id == selectedKeyframeId_;
                        }),
                    channel->keyframes.end());
                removed = channel->keyframes.size() != oldSize;
            }
            SEQUENCER::NormalizeCameraLensTrack(sequence.cameraLensTrack);
        }
        if (removed) {
            ClearSelection();
        }
        return removed;
    }

    bool CameraTimelineKeyframeEditor::DrawSelectedKeyInspector(
        CinematicSequence& sequence,
        bool editingAllowed,
        bool snapEnabled,
        float snapFramesPerSecond) {

        return DrawCameraTimelineKeyframeInspector(
            sequence,
            GetSelection(),
            editingAllowed,
            snapEnabled,
            snapFramesPerSecond);
    }

    void CameraTimelineKeyframeEditor::SelectTransformKeyframe(
        SEQUENCER::SequenceBindingId bindingId,
        uint64_t keyframeId) noexcept {

        selectedKind_ = keyframeId != 0
            ? CameraTimelineKeyframeKind::Transform
            : CameraTimelineKeyframeKind::None;
        selectedBindingId_ = keyframeId != 0
            ? bindingId
            : SEQUENCER::SequenceBindingId{};
        selectedKeyframeId_ = keyframeId;
    }

    void CameraTimelineKeyframeEditor::SelectLensKeyframe(
        SEQUENCER::SequenceBindingId bindingId,
        uint64_t keyframeId) noexcept {

        selectedKind_ = keyframeId != 0
            ? CameraTimelineKeyframeKind::Lens
            : CameraTimelineKeyframeKind::None;
        selectedBindingId_ = keyframeId != 0
            ? bindingId
            : SEQUENCER::SequenceBindingId{};
        selectedKeyframeId_ = keyframeId;
    }

} // namespace HIKARI::EDITOR
