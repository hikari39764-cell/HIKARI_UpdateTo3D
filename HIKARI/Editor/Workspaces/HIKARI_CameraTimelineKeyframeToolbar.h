#pragma once

#include "Editor/Workspaces/HIKARI_CameraTimelineCanvas.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    struct CameraTimelineKeyframeToolbarResult {
        bool sequenceChanged = false;
        bool previewRequested = false;
    };

    CameraTimelineKeyframeToolbarResult DrawCameraTimelineKeyframeToolbar(
        SceneDocument& document,
        CinematicSequence& sequence,
        SceneObjectId selectedCameraObjectId,
        float playheadTimeSeconds,
        bool editingAllowed,
        CameraTimelineCanvas& canvas);

} // namespace HIKARI::EDITOR
