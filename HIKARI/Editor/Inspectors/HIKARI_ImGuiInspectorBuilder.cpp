#include "HIKARI_ImGuiInspectorBuilder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Scene/HIKARI_SceneCatalog.h"

#if defined(_DEBUG)
#include "imgui.h"
#include <cstring>
#endif

namespace HIKARI {

    namespace {
        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string ShortGuid(std::string_view guid) {
            if (guid.size() <= 8) {
                return std::string(guid);
            }
            return std::string(guid.substr(0, 8));
        }

        struct AssetPickerEntry {
            std::string id{};
            std::string name{};
            std::string path{};
            std::string state{};
            std::string label{};
            std::string searchText{};
        };

        AssetPickerEntry BuildAssetPickerEntry(
            const AssetDescriptor& descriptor,
            const AssetDatabase* assetDatabase) {

            AssetPickerEntry entry{};
            entry.id = descriptor.id.value;
            entry.path = descriptor.sourcePath;

            if (assetDatabase) {
                if (const AssetRecord* record = assetDatabase->FindByGuid(AssetGuid{ descriptor.id.value })) {
                    entry.name = record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName;
                    entry.path = record->sourcePath.generic_string();
                    entry.state = ToString(GetImportState(*record));
                }
            }

            if (entry.name.empty()) {
                std::filesystem::path sourcePath{ descriptor.sourcePath };
                entry.name = sourcePath.stem().string();
            }
            if (entry.name.empty()) {
                entry.name = entry.id.empty() ? "<unnamed>" : entry.id;
            }
            if (entry.state.empty()) {
                entry.state = "Registered";
            }

            entry.label = entry.name + "  [" + entry.state + "]##" + entry.id;
            entry.searchText = ToLowerCopy(entry.name + " " + entry.path + " " + entry.id + " " + entry.state);
            return entry;
        }
    }

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
        if (context_.assetDatabase) {
            return EDITOR::DrawAssetField(
                context_.assetDatabase,
                EDITOR::AssetFieldOptions{
                    label,
                    assetType,
                    true,
                    false,
                    true
                },
                value);
        }

        if (!context_.assetRegistry) {
            return String(label, value);
        }

        std::vector<const AssetDescriptor*> assets = context_.assetRegistry->CollectByType(assetType);
        std::vector<AssetPickerEntry> entries;
        entries.reserve(assets.size());
        for (const AssetDescriptor* descriptor : assets) {
            if (descriptor && !descriptor->id.value.empty()) {
                entries.push_back(BuildAssetPickerEntry(*descriptor, context_.assetDatabase));
            }
        }
        std::sort(entries.begin(), entries.end(), [](const AssetPickerEntry& lhs, const AssetPickerEntry& rhs) {
            return ToLowerCopy(lhs.name) < ToLowerCopy(rhs.name);
        });

        const AssetPickerEntry* current = nullptr;
        for (const AssetPickerEntry& entry : entries) {
            if (entry.id == value) {
                current = &entry;
                break;
            }
        }

        bool changed = false;
        const std::string labelText(label);
        const std::string previewText = value.empty()
            ? std::string("<none>")
            : (current ? current->name : ("Missing: " + value));

        if (ImGui::BeginCombo(labelText.c_str(), previewText.c_str())) {
            static char searchBuffer[128]{};
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("Search##AssetPicker", searchBuffer, sizeof(searchBuffer));
            const std::string search = ToLowerCopy(searchBuffer);

            const bool isNoneSelected = value.empty();
            if (ImGui::Selectable("<none>", isNoneSelected)) {
                value.clear();
                changed = true;
            }
            if (isNoneSelected) {
                ImGui::SetItemDefaultFocus();
            }

            if (entries.empty()) {
                ImGui::TextDisabled("No matching assets");
            }

            for (const AssetPickerEntry& entry : entries) {
                if (!search.empty() && entry.searchText.find(search) == std::string::npos) {
                    continue;
                }

                const bool selected = (value == entry.id);
                ImGui::PushID(entry.id.c_str());
                if (ImGui::Selectable(entry.label.c_str(), selected)) {
                    value = entry.id;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%s\nGUID: %s",
                        entry.name.c_str(),
                        entry.path.empty() ? "<no source path>" : entry.path.c_str(),
                        entry.id.c_str());
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        if (!value.empty()) {
            ImGui::SameLine();
            const std::string copyLabel = "Copy##" + labelText;
            if (ImGui::SmallButton(copyLabel.c_str())) {
                ImGui::SetClipboardText(value.c_str());
            }

            if (current) {
                ImGui::TextDisabled("%s | %s | %s",
                    current->path.empty() ? "<no path>" : current->path.c_str(),
                    current->state.c_str(),
                    ShortGuid(current->id).c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Unresolved asset id: %s", value.c_str());
            }
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
