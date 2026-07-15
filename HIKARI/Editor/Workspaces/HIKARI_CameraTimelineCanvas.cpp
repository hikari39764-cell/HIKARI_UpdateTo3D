#include "Editor/Workspaces/HIKARI_CameraTimelineCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
        constexpr float kTimelineRulerHeight = 28.0f;
        constexpr float kTimelineTrackHeight = 58.0f;
        constexpr float kTimelineTrackLabelWidth = 112.0f;
        constexpr float kShotEdgeGrabWidth = 7.0f;

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

        const char* CameraLabel(
            const SceneDocument& document,
            SceneObjectId cameraObjectId) {

            const auto found = std::find_if(
                document.objects.begin(),
                document.objects.end(),
                [cameraObjectId](const SceneObjectData& object) {
                    return object.id == cameraObjectId;
                });
            return found != document.objects.end() && !found->name.empty()
                ? found->name.c_str()
                : "Missing Camera";
        }

#if defined(HIKARI_WITH_EDITOR)
        ImU32 ShotColor(SceneObjectId cameraObjectId, bool selected) {
            const uint32_t hash = static_cast<uint32_t>(
                cameraObjectId.value ^ (cameraObjectId.value >> 32u));
            const float hue = static_cast<float>(hash % 360u) / 360.0f;
            ImVec4 color{};
            ImGui::ColorConvertHSVtoRGB(
                hue,
                selected ? 0.55f : 0.48f,
                selected ? 0.92f : 0.72f,
                color.x,
                color.y,
                color.z);
            color.w = 1.0f;
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        float ChooseMajorTickStep(float pixelsPerSecond) {
            if (pixelsPerSecond >= 150.0f) {
                return 0.5f;
            }
            if (pixelsPerSecond >= 70.0f) {
                return 1.0f;
            }
            if (pixelsPerSecond >= 35.0f) {
                return 2.0f;
            }
            return 5.0f;
        }
#endif
    }

    CameraTimelineCanvasResult CameraTimelineCanvas::Draw(
        const SceneDocument& document,
        CameraCinematicSequence& sequence,
        float playheadTimeSeconds,
        bool playing,
        bool editingAllowed) {

        CameraTimelineCanvasResult result{};
        result.playheadTimeSeconds = playheadTimeSeconds;
#if defined(HIKARI_WITH_EDITOR)
        const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x = (std::max)(canvasSize.x, 320.0f);
        canvasSize.y = (std::max)(canvasSize.y, 104.0f);
        ImGui::InvisibleButton(
            "##CameraTimelineCanvas",
            canvasSize,
            ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonMiddle);
        const bool canvasHovered = ImGui::IsItemHovered();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const float timelineLeft = canvasPosition.x + kTimelineTrackLabelWidth;
        const float timelineRight = canvasPosition.x + canvasSize.x;
        const float timelineWidth = (std::max)(timelineRight - timelineLeft, 1.0f);
        const float trackTop = canvasPosition.y + kTimelineRulerHeight;
        const float trackBottom = (std::min)(
            trackTop + kTimelineTrackHeight,
            canvasPosition.y + canvasSize.y);

        if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
            const float mouseTimelineX = std::clamp(
                ImGui::GetIO().MousePos.x,
                timelineLeft,
                timelineRight);
            const float anchorTime = scrollTimeSeconds_ +
                (mouseTimelineX - timelineLeft) / pixelsPerSecond_;
            const float zoomFactor = ImGui::GetIO().MouseWheel > 0.0f
                ? 1.15f
                : 1.0f / 1.15f;
            pixelsPerSecond_ = std::clamp(
                pixelsPerSecond_ * zoomFactor,
                24.0f,
                220.0f);
            scrollTimeSeconds_ = anchorTime -
                (mouseTimelineX - timelineLeft) / pixelsPerSecond_;
        }

        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            panning_ = true;
            panStartMouseX_ = ImGui::GetIO().MousePos.x;
            panStartScrollTime_ = scrollTimeSeconds_;
        }
        if (panning_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                scrollTimeSeconds_ = panStartScrollTime_ -
                    (ImGui::GetIO().MousePos.x - panStartMouseX_) /
                        pixelsPerSecond_;
            } else {
                panning_ = false;
            }
        }

        const float visibleDuration = timelineWidth / pixelsPerSecond_;
        const float maxScroll = (std::max)(
            sequence.durationSeconds - visibleDuration,
            0.0f);
        scrollTimeSeconds_ = std::clamp(
            scrollTimeSeconds_,
            0.0f,
            maxScroll);
        if (playing) {
            if (playheadTimeSeconds < scrollTimeSeconds_) {
                scrollTimeSeconds_ = playheadTimeSeconds;
            } else if (playheadTimeSeconds >
                    scrollTimeSeconds_ + visibleDuration) {
                scrollTimeSeconds_ = playheadTimeSeconds -
                    visibleDuration * 0.85f;
            }
            scrollTimeSeconds_ = std::clamp(
                scrollTimeSeconds_,
                0.0f,
                maxScroll);
        }

        const ImU32 backgroundColor = IM_COL32(19, 23, 30, 255);
        const ImU32 rulerColor = IM_COL32(30, 36, 46, 255);
        const ImU32 labelColor = IM_COL32(25, 30, 39, 255);
        const ImU32 gridColor = IM_COL32(64, 72, 87, 150);
        const ImU32 borderColor = IM_COL32(78, 88, 105, 210);
        drawList->AddRectFilled(
            canvasPosition,
            ImVec2(timelineRight, canvasPosition.y + canvasSize.y),
            backgroundColor,
            4.0f);
        drawList->AddRectFilled(
            canvasPosition,
            ImVec2(timelineRight, trackTop),
            rulerColor,
            4.0f,
            ImDrawFlags_RoundCornersTop);
        drawList->AddRectFilled(
            ImVec2(canvasPosition.x, trackTop),
            ImVec2(timelineLeft, trackBottom),
            labelColor);
        drawList->AddRect(
            canvasPosition,
            ImVec2(timelineRight, canvasPosition.y + canvasSize.y),
            borderColor,
            4.0f);
        drawList->AddLine(
            ImVec2(timelineLeft, canvasPosition.y),
            ImVec2(timelineLeft, canvasPosition.y + canvasSize.y),
            borderColor);
        drawList->AddText(
            ImVec2(canvasPosition.x + 10.0f, trackTop + 20.0f),
            IM_COL32(205, 211, 222, 255),
            "Camera Shots");

        const float majorStep = ChooseMajorTickStep(pixelsPerSecond_);
        const float firstMajorTime =
            std::floor(scrollTimeSeconds_ / majorStep) * majorStep;
        for (float time = firstMajorTime;
            time <= scrollTimeSeconds_ + visibleDuration + majorStep;
            time += majorStep) {
            const float x = timelineLeft +
                (time - scrollTimeSeconds_) * pixelsPerSecond_;
            if (x < timelineLeft || x > timelineRight) {
                continue;
            }
            drawList->AddLine(
                ImVec2(x, canvasPosition.y + 17.0f),
                ImVec2(x, trackBottom),
                gridColor);
            char timeLabel[32]{};
            std::snprintf(timeLabel, sizeof(timeLabel), "%.1f", time);
            drawList->AddText(
                ImVec2(x + 3.0f, canvasPosition.y + 3.0f),
                IM_COL32(164, 174, 191, 255),
                timeLabel);
        }

        CinematicShotClip* hoveredShot = nullptr;
        DragMode hoveredDragMode = DragMode::None;
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        for (CinematicShotClip& shot : sequence.shots) {
            const float shotLeft = timelineLeft +
                (shot.startTimeSeconds - scrollTimeSeconds_) *
                    pixelsPerSecond_;
            const float shotRight = shotLeft +
                shot.durationSeconds * pixelsPerSecond_;
            const float visibleLeft = (std::max)(shotLeft, timelineLeft);
            const float visibleRight = (std::min)(shotRight, timelineRight);
            if (visibleRight <= visibleLeft) {
                continue;
            }

            const bool selected = shot.id == selectedShotId_;
            const ImVec2 shotMin{ visibleLeft, trackTop + 8.0f };
            const ImVec2 shotMax{ visibleRight, trackBottom - 8.0f };
            drawList->AddRectFilled(
                shotMin,
                shotMax,
                ShotColor(shot.cameraObjectId, selected),
                4.0f);
            drawList->AddRect(
                shotMin,
                shotMax,
                selected
                    ? IM_COL32(255, 222, 126, 255)
                    : IM_COL32(180, 190, 205, 210),
                4.0f,
                ImDrawFlags_None,
                selected ? 2.0f : 1.0f);
            drawList->PushClipRect(shotMin, shotMax, true);
            drawList->AddText(
                ImVec2(shotMin.x + 8.0f, shotMin.y + 9.0f),
                IM_COL32(244, 247, 251, 255),
                CameraLabel(document, shot.cameraObjectId));
            drawList->PopClipRect();

            if (mousePosition.x >= shotMin.x && mousePosition.x <= shotMax.x &&
                mousePosition.y >= shotMin.y && mousePosition.y <= shotMax.y) {
                hoveredShot = &shot;
                if (std::abs(mousePosition.x - shotLeft) <= kShotEdgeGrabWidth) {
                    hoveredDragMode = DragMode::ResizeLeft;
                } else if (std::abs(mousePosition.x - shotRight) <=
                        kShotEdgeGrabWidth) {
                    hoveredDragMode = DragMode::ResizeRight;
                } else {
                    hoveredDragMode = DragMode::Move;
                }
            }
        }

        if (canvasHovered && editingAllowed &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredShot != nullptr) {
                selectedShotId_ = hoveredShot->id;
                draggedShotId_ = hoveredShot->id;
                dragMode_ = hoveredDragMode;
                dragStartMouseX_ = mousePosition.x;
                dragStartShotTime_ = hoveredShot->startTimeSeconds;
                dragStartShotDuration_ = hoveredShot->durationSeconds;
            } else if (mousePosition.x >= timelineLeft) {
                scrubbing_ = true;
                result.playheadChanged = true;
                result.playheadTimeSeconds = std::clamp(
                    scrollTimeSeconds_ +
                        (mousePosition.x - timelineLeft) / pixelsPerSecond_,
                    0.0f,
                    sequence.durationSeconds);
            }
        }

        if (dragMode_ != DragMode::None) {
            if (editingAllowed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (CinematicShotClip* shot = FindShot(
                        sequence,
                        draggedShotId_)) {
                    const float deltaTimeSeconds =
                        (mousePosition.x - dragStartMouseX_) /
                            pixelsPerSecond_;
                    const float originalEnd = dragStartShotTime_ +
                        dragStartShotDuration_;
                    switch (dragMode_) {
                    case DragMode::Move:
                        shot->startTimeSeconds = (std::max)(
                            0.0f,
                            dragStartShotTime_ + deltaTimeSeconds);
                        break;
                    case DragMode::ResizeLeft:
                        shot->startTimeSeconds = std::clamp(
                            dragStartShotTime_ + deltaTimeSeconds,
                            0.0f,
                            originalEnd -
                                kMinCinematicShotDurationSeconds);
                        shot->durationSeconds = originalEnd -
                            shot->startTimeSeconds;
                        break;
                    case DragMode::ResizeRight:
                        shot->durationSeconds = (std::max)(
                            kMinCinematicShotDurationSeconds,
                            dragStartShotDuration_ + deltaTimeSeconds);
                        break;
                    default:
                        break;
                    }
                    sequence.durationSeconds = (std::max)(
                        sequence.durationSeconds,
                        shot->startTimeSeconds + shot->durationSeconds);
                    result.sequenceChanged = true;
                }
            } else {
                dragMode_ = DragMode::None;
                draggedShotId_ = 0;
                NormalizeCameraCinematicSequence(sequence);
            }
        }

        if (scrubbing_) {
            if (editingAllowed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                result.playheadChanged = true;
                result.playheadTimeSeconds = std::clamp(
                    scrollTimeSeconds_ +
                        (mousePosition.x - timelineLeft) / pixelsPerSecond_,
                    0.0f,
                    sequence.durationSeconds);
            } else {
                scrubbing_ = false;
            }
        }

        const float playheadX = timelineLeft +
            (result.playheadTimeSeconds - scrollTimeSeconds_) *
                pixelsPerSecond_;
        if (playheadX >= timelineLeft && playheadX <= timelineRight) {
            const ImU32 playheadColor = IM_COL32(255, 111, 92, 255);
            drawList->AddLine(
                ImVec2(playheadX, canvasPosition.y),
                ImVec2(playheadX, canvasPosition.y + canvasSize.y),
                playheadColor,
                2.0f);
            drawList->AddTriangleFilled(
                ImVec2(playheadX - 5.0f, canvasPosition.y),
                ImVec2(playheadX + 5.0f, canvasPosition.y),
                ImVec2(playheadX, canvasPosition.y + 7.0f),
                playheadColor);
        }

        if (canvasHovered && hoveredShot != nullptr) {
            ImGui::SetMouseCursor(
                hoveredDragMode == DragMode::Move
                    ? ImGuiMouseCursor_Hand
                    : ImGuiMouseCursor_ResizeEW);
            ImGui::SetTooltip(
                "%s\nStart %.2f s  Duration %.2f s",
                CameraLabel(document, hoveredShot->cameraObjectId),
                hoveredShot->startTimeSeconds,
                hoveredShot->durationSeconds);
        }
#else
        (void)document;
        (void)sequence;
        (void)playing;
        (void)editingAllowed;
#endif
        return result;
    }

    void CameraTimelineCanvas::Reset() {
        pixelsPerSecond_ = 90.0f;
        scrollTimeSeconds_ = 0.0f;
        selectedShotId_ = 0;
        CancelInteraction();
    }

    void CameraTimelineCanvas::CancelInteraction() {
        draggedShotId_ = 0;
        dragMode_ = DragMode::None;
        scrubbing_ = false;
        panning_ = false;
    }

    uint64_t CameraTimelineCanvas::GetSelectedShotId() const noexcept {
        return selectedShotId_;
    }

    void CameraTimelineCanvas::SetSelectedShotId(uint64_t shotId) noexcept {
        selectedShotId_ = shotId;
    }

    float CameraTimelineCanvas::GetPixelsPerSecond() const noexcept {
        return pixelsPerSecond_;
    }

    void CameraTimelineCanvas::SetPixelsPerSecond(
        float pixelsPerSecond) noexcept {

        pixelsPerSecond_ = std::clamp(pixelsPerSecond, 24.0f, 220.0f);
    }

} // namespace HIKARI::EDITOR
