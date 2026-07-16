#pragma once

#include <string>

namespace HIKARI::EDITOR {

    struct EditorDocumentMenuState {
        std::string undoLabel{};
        std::string redoLabel{};
        bool canUndo = false;
        bool canRedo = false;
        bool saveRequested = false;
        bool undoRequested = false;
        bool redoRequested = false;
    };

    void DrawEditorDocumentMenu(EditorDocumentMenuState& state);

} // namespace HIKARI::EDITOR
