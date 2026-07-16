#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void DrawEditorDocumentMenu(EditorDocumentMenuState& state) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::BeginMenu("Edit")) {
            return;
        }

        const std::string undoText = state.undoLabel.empty()
            ? "Undo"
            : "Undo " + state.undoLabel;
        const std::string redoText = state.redoLabel.empty()
            ? "Redo"
            : "Redo " + state.redoLabel;
        state.undoRequested |= ImGui::MenuItem(
            undoText.c_str(),
            "Ctrl+Z",
            false,
            state.canUndo);
        state.redoRequested |= ImGui::MenuItem(
            redoText.c_str(),
            "Ctrl+Y / Ctrl+Shift+Z",
            false,
            state.canRedo);
        ImGui::Separator();
        state.saveRequested |= ImGui::MenuItem(
            "Save Current Scene",
            "Ctrl+S");
        ImGui::EndMenu();
#else
        (void)state;
#endif
    }

} // namespace HIKARI::EDITOR
