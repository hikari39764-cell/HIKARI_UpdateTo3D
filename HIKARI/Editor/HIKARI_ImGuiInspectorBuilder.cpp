#include "HIKARI_ImGuiInspectorBuilder.h"

#if defined(_DEBUG)
#include "imgui.h"
#include <cstring>
#endif

namespace HIKARI {

    bool ImGuiInspectorBuilder::Bool(std::string_view label, bool& value) {
#if defined(_DEBUG)
        return ImGui::Checkbox(std::string(label).c_str(), &value);
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::String(std::string_view label, std::string& value) {
#if defined(_DEBUG)
        char buffer[256]{};
        strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
        if (ImGui::InputText(std::string(label).c_str(), buffer, sizeof(buffer))) {
            value = buffer;
            return true;
        }
        return false;
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

} // namespace HIKARI
