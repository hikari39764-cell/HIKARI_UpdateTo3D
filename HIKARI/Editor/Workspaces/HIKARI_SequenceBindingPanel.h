#pragma once

#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    struct SequenceBindingPanelResult {
        bool sequenceChanged = false;
    };

    SEQUENCER::SequenceBindingId FindOrCreatePortableCameraBinding(
        const SceneDocument& document,
        CinematicSequence& sequence,
        SceneObjectId cameraObjectId,
        SEQUENCER::SequenceBindingContext& previewBindings);

    class SequenceBindingPanel {
    public:
        SequenceBindingPanelResult Draw(
            const SceneDocument& document,
            CinematicSequence& sequence,
            bool portableAsset,
            bool editingAllowed);

        SEQUENCER::SequenceBindingContext& GetPreviewBindings() noexcept;
        const SEQUENCER::SequenceBindingContext& GetPreviewBindings()
            const noexcept;
        void Reset();

    private:
        SEQUENCER::SequenceBindingContext previewBindings_{};
    };

} // namespace HIKARI::EDITOR
