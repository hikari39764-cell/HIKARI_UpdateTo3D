#pragma once

#include "Editor/Workspaces/HIKARI_CameraTimelineKeyframeEditor.h"

namespace HIKARI::EDITOR {

    bool DrawCameraTimelineKeyframeInspector(
        CinematicSequence& sequence,
        const CameraTimelineKeyframeSelection& selection,
        bool editingAllowed,
        bool snapEnabled,
        float snapFramesPerSecond);

} // namespace HIKARI::EDITOR
