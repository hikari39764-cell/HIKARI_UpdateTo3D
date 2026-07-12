#pragma once

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    enum class EditorButtonTone {
        Neutral,
        Primary,
        Quiet,
        Danger,
    };

    enum class EditorStatusTone {
        Normal,
        Ready,
        Warning,
        Error,
    };

#if defined(HIKARI_WITH_EDITOR)
    bool ActionButton(
        const char* label,
        const char* id,
        EditorButtonTone tone = EditorButtonTone::Neutral,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const char* tooltip = nullptr);

    bool ToggleButton(
        const char* label,
        const char* id,
        bool selected,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const char* tooltip = nullptr);

    bool ModeButton(
        const char* label,
        const char* id,
        bool selected,
        float height = 28.0f);

    void ToolbarLabel(const char* label);
    void StatusText(const char* label, EditorStatusTone tone);

    bool BeginPropertyTable(const char* id, float labelWidth = 132.0f);
    void PropertyLabel(const char* label);
    void EndPropertyTable();
#endif

} // namespace HIKARI::EDITOR
