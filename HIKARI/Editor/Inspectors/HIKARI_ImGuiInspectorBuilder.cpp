#include "HIKARI_ImGuiInspectorBuilder.h"

#include <algorithm>
#include <string>
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
        std::vector<std::string> assetIds;
        assetIds.reserve(assets.size());
        for (const AssetDescriptor* descriptor : assets) {
            if (descriptor && !descriptor->id.value.empty()) {
                assetIds.push_back(descriptor->id.value);
            }
        }
        std::sort(assetIds.begin(), assetIds.end());

        bool changed = false;
        const std::string labelText(label);
        const std::string previewText = value.empty() ? std::string("<none>") : value;
        if (ImGui::BeginCombo(labelText.c_str(), previewText.c_str())) {
            const bool isNoneSelected = value.empty();
            if (ImGui::Selectable("<none>", isNoneSelected)) {
                value.clear();
                changed = true;
            }
            if (isNoneSelected) {
                ImGui::SetItemDefaultFocus();
            }

            if (assetIds.empty()) {
                ImGui::TextDisabled("No matching assets");
            }

            for (const std::string& assetId : assetIds) {
                const bool selected = (value == assetId);
                ImGui::PushID(assetId.c_str());
                if (ImGui::Selectable(assetId.c_str(), selected)) {
                    value = assetId;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
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
        const std::string labelText(label);
        const std::string previewText = value.empty() ? std::string("<none>") : value;
        if (ImGui::BeginCombo(labelText.c_str(), previewText.c_str())) {
            if (sceneIds.empty()) {
                ImGui::TextDisabled("No scenes registered");
            }
            for (const std::string& sceneId : sceneIds) {
                const bool selected = (sceneId == value);
                ImGui::PushID(sceneId.c_str());
                if (ImGui::Selectable(sceneId.c_str(), selected)) {
                    value = sceneId;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
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
