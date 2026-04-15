#include "HIKARI_ImGuiInspectorBuilder.h"

#include <algorithm>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Scene/HIKARI_SceneCatalog.h"

#if defined(_DEBUG)
#include "imgui.h"
#include <cstring>
#endif

namespace HIKARI {

    void ImGuiInspectorBuilder::SetContext(const InspectorContext& context) {
        context_ = context;
    }

    bool ImGuiInspectorBuilder::Bool(std::string_view label, bool& value) {
#if defined(_DEBUG)
        return ImGui::Checkbox(std::string(label).c_str(), &value);
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::Int(std::string_view label, int& value) {
#if defined(_DEBUG)
        return ImGui::InputInt(std::string(label).c_str(), &value);
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::Float(std::string_view label, float& value) {
#if defined(_DEBUG)
        return ImGui::DragFloat(std::string(label).c_str(), &value, 0.1f);
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

    bool ImGuiInspectorBuilder::Vec2(std::string_view label, float& x, float& y) {
#if defined(_DEBUG)
        float values[2]{ x, y };
        if (!ImGui::DragFloat2(std::string(label).c_str(), values, 1.0f)) {
            return false;
        }

        x = values[0];
        y = values[1];
        return true;
#else
        (void)label;
        (void)x;
        (void)y;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) {
#if defined(_DEBUG)
        if (!context_.assetRegistry) {
            return String(label, value);
        }

        std::vector<const AssetDescriptor*> assets = context_.assetRegistry->CollectByType(assetType);
        std::sort(assets.begin(), assets.end(),
            [](const AssetDescriptor* lhs, const AssetDescriptor* rhs) {
                const std::string left = lhs ? lhs->id.value : std::string{};
                const std::string right = rhs ? rhs->id.value : std::string{};
                return left < right;
            });

        bool changed = false;
        const char* preview = value.empty() ? "<none>" : value.c_str();
        if (ImGui::BeginCombo(std::string(label).c_str(), preview)) {
            const bool isNoneSelected = value.empty();
            if (ImGui::Selectable("<none>", isNoneSelected)) {
                value.clear();
                changed = true;
            }
            if (isNoneSelected) {
                ImGui::SetItemDefaultFocus();
            }

            for (const AssetDescriptor* descriptor : assets) {
                if (!descriptor) {
                    continue;
                }
                const bool selected = (value == descriptor->id.value);
                if (ImGui::Selectable(descriptor->id.value.c_str(), selected)) {
                    value = descriptor->id.value;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        return changed;
#else
        (void)label;
        (void)assetType;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::SceneIdPicker(std::string_view label, std::string& value) {
#if defined(_DEBUG)
        if (!context_.sceneCatalog) {
            return String(label, value);
        }

        std::vector<std::string> sceneIds = context_.sceneCatalog->GetSceneIds();
        std::sort(sceneIds.begin(), sceneIds.end());

        bool changed = false;
        const char* preview = value.empty() ? "<none>" : value.c_str();
        if (ImGui::BeginCombo(std::string(label).c_str(), preview)) {
            for (const std::string& sceneId : sceneIds) {
                const bool selected = (sceneId == value);
                if (ImGui::Selectable(sceneId.c_str(), selected)) {
                    value = sceneId;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        return changed;
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

} // namespace HIKARI
