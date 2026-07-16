#include "Editor/Workspaces/HIKARI_CameraTimelineKeyframeToolbar.h"

#include "Editor/Workspaces/HIKARI_CameraTimelineKeyframeAuthoring.h"
#include "Editor/Workspaces/HIKARI_SequenceBindingPanel.h"

#include <string>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    CameraTimelineKeyframeToolbarResult DrawCameraTimelineKeyframeToolbar(
        SceneDocument& document,
        CinematicSequence& sequence,
        SceneObjectId selectedCameraObjectId,
        float playheadTimeSeconds,
        bool editingAllowed,
        bool portableAsset,
        SEQUENCER::SequenceBindingContext* previewBindings,
        CameraTimelineCanvas& canvas) {

        CameraTimelineKeyframeToolbarResult result{};
#if defined(HIKARI_WITH_EDITOR)
        const bool canCapture = editingAllowed &&
            selectedCameraObjectId.value != 0;
        const auto resolveCaptureBinding = [&]() {
            return portableAsset && previewBindings != nullptr
                ? FindOrCreatePortableCameraBinding(
                    document,
                    sequence,
                    selectedCameraObjectId,
                    *previewBindings)
                : SEQUENCER::SequenceBindingId{};
        };
        if (!canCapture) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Add Transform Key")) {
            const CameraKeyframeCaptureResult capture =
                CaptureCameraTransformKeyframe(
                    document,
                    sequence,
                    selectedCameraObjectId,
                    playheadTimeSeconds,
                    resolveCaptureBinding());
            if (capture.Succeeded()) {
                canvas.SelectTransformKeyframe(
                    capture.cameraBindingId,
                    capture.transformKeyframeId);
                result.sequenceChanged = true;
                result.previewRequested = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Lens Key")) {
            const CameraKeyframeCaptureResult capture =
                CaptureCameraLensKeyframe(
                    document,
                    sequence,
                    selectedCameraObjectId,
                    playheadTimeSeconds,
                    resolveCaptureBinding());
            if (capture.Succeeded()) {
                canvas.SelectLensKeyframe(
                    capture.cameraBindingId,
                    capture.lensKeyframeId);
                result.sequenceChanged = true;
                result.previewRequested = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Camera Key")) {
            const CameraKeyframeCaptureResult capture =
                CaptureCameraKeyframe(
                    document,
                    sequence,
                    selectedCameraObjectId,
                    playheadTimeSeconds,
                    resolveCaptureBinding());
            if (capture.CapturedBoth()) {
                canvas.SelectTransformKeyframe(
                    capture.cameraBindingId,
                    capture.transformKeyframeId);
                result.sequenceChanged = true;
                result.previewRequested = true;
            }
        }
        if (!canCapture) {
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                canCapture
                    ? "Capture transform and lens at the playhead"
                    : "Select a Camera object to capture a keyframe");
        }

        ImGui::SameLine();
        const bool canDelete = editingAllowed &&
            canvas.HasSelectedKeyframe();
        if (!canDelete) {
            ImGui::BeginDisabled();
        }
        const size_t selectedKeyCount = canvas.GetSelectedKeyframeCount();
        const std::string deleteLabel = selectedKeyCount > 1
            ? "Delete Keys (" + std::to_string(selectedKeyCount) + ")"
            : "Delete Key";
        if (ImGui::Button(deleteLabel.c_str())) {
            const bool deleted = canvas.DeleteSelectedKeyframe(sequence);
            result.sequenceChanged |= deleted;
            result.previewRequested |= deleted;
        }
        if (!canDelete) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (!editingAllowed) {
            ImGui::BeginDisabled();
        }
        bool transformEnabled = sequence.cameraTransformTrack.enabled;
        if (ImGui::Checkbox("Transform Track", &transformEnabled)) {
            sequence.cameraTransformTrack.enabled = transformEnabled;
            result.sequenceChanged = true;
            result.previewRequested = true;
        }
        ImGui::SameLine();
        bool lensEnabled = sequence.cameraLensTrack.enabled;
        if (ImGui::Checkbox("Lens Track", &lensEnabled)) {
            sequence.cameraLensTrack.enabled = lensEnabled;
            result.sequenceChanged = true;
            result.previewRequested = true;
        }
        if (!editingAllowed) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        bool snapEnabled = canvas.IsSnapEnabled();
        if (ImGui::Checkbox("Snap", &snapEnabled)) {
            canvas.SetSnapEnabled(snapEnabled);
        }
        ImGui::SameLine();
        int snapFramesPerSecond = canvas.GetSnapFramesPerSecond();
        ImGui::SetNextItemWidth(82.0f);
        if (ImGui::DragInt(
                "##CameraTimelineSnapRate",
                &snapFramesPerSecond,
                1.0f,
                1,
                240,
                "%d fps")) {
            canvas.SetSnapFramesPerSecond(snapFramesPerSecond);
        }

        if (canvas.DrawSelectedKeyframeInspector(
                sequence,
                editingAllowed)) {
            result.sequenceChanged = true;
            result.previewRequested = true;
        }

        ImGui::TextDisabled(
            "Shift: add | Ctrl: toggle | Drag empty lane: box select | "
            "Ctrl+A/C/V/D | Delete");
#else
        (void)document;
        (void)sequence;
        (void)selectedCameraObjectId;
        (void)playheadTimeSeconds;
        (void)editingAllowed;
        (void)portableAsset;
        (void)previewBindings;
        (void)canvas;
#endif
        return result;
    }

} // namespace HIKARI::EDITOR
