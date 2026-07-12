#include "Editor/Style/HIKARI_EditorWidgets.h"

#if defined(HIKARI_WITH_EDITOR)

namespace HIKARI::EDITOR {

    namespace {
        struct ButtonPalette {
            ImVec4 normal;
            ImVec4 hovered;
            ImVec4 active;
            ImVec4 text;
        };

        ButtonPalette ResolvePalette(EditorButtonTone tone) {
            switch (tone) {
            case EditorButtonTone::Primary:
                return {
                    { 0.10f, 0.39f, 0.43f, 1.00f },
                    { 0.13f, 0.49f, 0.53f, 1.00f },
                    { 0.09f, 0.33f, 0.37f, 1.00f },
                    { 0.92f, 0.98f, 0.98f, 1.00f }
                };
            case EditorButtonTone::Quiet:
                return {
                    { 0.10f, 0.12f, 0.14f, 0.72f },
                    { 0.17f, 0.20f, 0.23f, 1.00f },
                    { 0.13f, 0.25f, 0.27f, 1.00f },
                    { 0.76f, 0.80f, 0.83f, 1.00f }
                };
            case EditorButtonTone::Danger:
                return {
                    { 0.43f, 0.16f, 0.15f, 1.00f },
                    { 0.56f, 0.20f, 0.18f, 1.00f },
                    { 0.36f, 0.12f, 0.12f, 1.00f },
                    { 1.00f, 0.93f, 0.92f, 1.00f }
                };
            case EditorButtonTone::Neutral:
            default:
                return {
                    { 0.15f, 0.17f, 0.20f, 1.00f },
                    { 0.21f, 0.25f, 0.28f, 1.00f },
                    { 0.16f, 0.31f, 0.33f, 1.00f },
                    { 0.88f, 0.91f, 0.93f, 1.00f }
                };
            }
        }

        ImVec4 ResolveStatusColor(EditorStatusTone tone) {
            switch (tone) {
            case EditorStatusTone::Ready: return { 0.39f, 0.86f, 0.61f, 1.00f };
            case EditorStatusTone::Warning: return { 0.94f, 0.69f, 0.27f, 1.00f };
            case EditorStatusTone::Error: return { 0.96f, 0.38f, 0.34f, 1.00f };
            case EditorStatusTone::Normal:
            default: return { 0.66f, 0.72f, 0.77f, 1.00f };
            }
        }
    }

    bool ActionButton(
        const char* label,
        const char* id,
        EditorButtonTone tone,
        const ImVec2& size,
        const char* tooltip) {
        const ButtonPalette palette = ResolvePalette(tone);
        ImGui::PushID(id);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, palette.normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.active);
        ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
        const bool pressed = ImGui::Button(label, size);
        if (tooltip != nullptr && tooltip[0] != '\0' && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::PopID();
        return pressed;
    }

    bool ToggleButton(
        const char* label,
        const char* id,
        bool selected,
        const ImVec2& size,
        const char* tooltip) {
        return ActionButton(
            label,
            id,
            selected ? EditorButtonTone::Primary : EditorButtonTone::Quiet,
            size,
            tooltip);
    }

    bool ModeButton(
        const char* label,
        const char* id,
        bool selected,
        float height) {
        return ToggleButton(
            label,
            id,
            selected,
            ImVec2(-1.0f, height));
    }

    void ToolbarLabel(const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
    }

    void StatusText(const char* label, EditorStatusTone tone) {
        ImGui::TextColored(ResolveStatusColor(tone), "%s", label);
    }

    bool BeginPropertyTable(const char* id, float labelWidth) {
        if (!ImGui::BeginTable(
                id,
                2,
                ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_NoSavedSettings |
                    ImGuiTableFlags_PadOuterX)) {
            return false;
        }
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void PropertyLabel(const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
    }

    void EndPropertyTable() {
        ImGui::EndTable();
    }

} // namespace HIKARI::EDITOR

#endif
