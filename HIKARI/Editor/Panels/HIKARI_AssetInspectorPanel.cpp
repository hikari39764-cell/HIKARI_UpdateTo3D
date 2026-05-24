#include "HIKARI_AssetInspectorPanel.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/HIKARI_EditorSelection.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        const char* ToString(AssetType type) {
            switch (type) {
            case AssetType::Model: return "Model";
            case AssetType::Sky: return "Sky";
            case AssetType::Texture: return "Texture";
            case AssetType::Material: return "Material";
            case AssetType::Animation: return "Animation";
            case AssetType::Particle: return "Particle";
            case AssetType::VfxEffect: return "VfxEffect";
            case AssetType::Unknown:
            default: return "Unknown";
            }
        }

        const char* TextureUsageItems[] = {
            "Auto", "BaseColor", "Normal", "MetallicRoughness", "Occlusion", "Emissive",
            "Mask", "UI", "SkyCubemap", "IblIrradiance", "IblPrefiltered", "BrdfLut"
        };

        const char* TextureColorSpaceItems[] = {
            "Auto", "Linear", "Srgb"
        };

        const char* TextureCompressionItems[] = {
            "Auto", "None", "BC1", "BC3", "BC4", "BC5", "BC6H", "BC7"
        };

        const char* TextureMipPolicyItems[] = {
            "Auto", "Generate", "Preserve", "None"
        };

        int FindItemIndex(const char* const* items, int count, const std::string& value) {
            for (int i = 0; i < count; ++i) {
                if (value == items[i]) {
                    return i;
                }
            }
            return 0;
        }

        nlohmann::json ReadSettings(const AssetRecord& record) {
            nlohmann::json settings = nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
            if (!settings.is_object()) {
                settings = nlohmann::json::object();
            }
            return settings;
        }

        void CopyToBuffer(const std::string& text, char* buffer, size_t bufferSize) {
            if (bufferSize == 0) {
                return;
            }
            const size_t copySize = (std::min)(text.size(), bufferSize - 1);
            std::memcpy(buffer, text.data(), copySize);
            buffer[copySize] = '\0';
        }

#if defined(_DEBUG)
        bool DrawComboSetting(
            const char* label,
            nlohmann::json& settings,
            const char* key,
            const char* const* items,
            int itemCount) {

            int current = FindItemIndex(items, itemCount, settings.value(key, ""));
            if (ImGui::Combo(label, &current, items, itemCount)) {
                settings[key] = items[current];
                return true;
            }
            return false;
        }
#endif
    }

    void AssetInspectorPanel::Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const {
#if defined(_DEBUG)
        if (selection.selectedAssetGuid.empty()) {
            ImGui::TextDisabled("No Asset selected");
            return;
        }

        AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ selection.selectedAssetGuid });
        if (!record) {
            ImGui::TextDisabled("Selected Asset is no longer in the database");
            return;
        }

        static std::string loadedGuid{};
        static std::array<char, 256> displayNameBuffer{};
        static std::array<char, 8192> importSettingsBuffer{};
        if (loadedGuid != record->guid.value) {
            loadedGuid = record->guid.value;
            CopyToBuffer(record->displayName, displayNameBuffer.data(), displayNameBuffer.size());
            CopyToBuffer(record->meta.importSettingsJson, importSettingsBuffer.data(), importSettingsBuffer.size());
        }

        ImGui::Text("GUID: %s", record->guid.value.c_str());
        ImGui::Text("Type: %s", ToString(record->type));
        ImGui::Text("Source Path: %s", record->sourcePath.generic_string().c_str());
        ImGui::Text("Meta Path: %s", record->metaPath.generic_string().c_str());
        ImGui::Text("Importer: %s", record->meta.importerId.c_str());
        ImGui::Text("Importer Version: %u", record->meta.importerVersion);
        ImGui::Text("Imported Directory: %s", record->importedDirectory.generic_string().c_str());
        ImGui::Text("Last Import: %s", record->lastImportSucceeded ? "Succeeded" : "Not Succeeded");
        if (!record->lastImportMessage.empty()) {
            ImGui::TextWrapped("%s", record->lastImportMessage.c_str());
        }

        ImGui::SeparatorText("Artifacts");
        if (record->meta.artifacts.empty()) {
            ImGui::TextDisabled("No artifacts");
        } else {
            for (const AssetArtifactDesc& artifact : record->meta.artifacts) {
                ImGui::BulletText("%s | %s | %s", artifact.role.c_str(), artifact.format.c_str(), artifact.path.c_str());
            }
        }

        ImGui::SeparatorText("Dependencies");
        if (record->meta.dependencies.empty()) {
            ImGui::TextDisabled("No dependencies");
        } else {
            for (const AssetDependencyDesc& dependency : record->meta.dependencies) {
                ImGui::BulletText("%s | %s | %s", dependency.role.c_str(), dependency.guid.value.c_str(), dependency.path.c_str());
            }
        }

        ImGui::SeparatorText("Editable");
        bool dirty = false;
        if (ImGui::InputText("Display Name", displayNameBuffer.data(), displayNameBuffer.size())) {
            dirty = true;
        }

        nlohmann::json settings = ReadSettings(*record);
        if (record->type == AssetType::Texture) {
            dirty = DrawComboSetting("Texture Usage", settings, "usage", TextureUsageItems, IM_ARRAYSIZE(TextureUsageItems)) || dirty;
            dirty = DrawComboSetting("Color Space", settings, "colorSpace", TextureColorSpaceItems, IM_ARRAYSIZE(TextureColorSpaceItems)) || dirty;
            dirty = DrawComboSetting("Compression", settings, "compression", TextureCompressionItems, IM_ARRAYSIZE(TextureCompressionItems)) || dirty;
            dirty = DrawComboSetting("Mip Policy", settings, "mipPolicy", TextureMipPolicyItems, IM_ARRAYSIZE(TextureMipPolicyItems)) || dirty;
        } else if (record->type == AssetType::Sky) {
            bool autoBakeIBL = settings.value("autoBakeIBL", false);
            int prefilteredMipCount = settings.value("prefilteredMipCount", 7);
            if (ImGui::Checkbox("Auto Bake IBL", &autoBakeIBL)) {
                settings["autoBakeIBL"] = autoBakeIBL;
                dirty = true;
            }
            if (ImGui::InputInt("Prefiltered Mip Count", &prefilteredMipCount)) {
                settings["prefilteredMipCount"] = (std::max)(0, prefilteredMipCount);
                dirty = true;
            }
        }

        if (dirty) {
            record->meta.displayName = displayNameBuffer.data();
            record->displayName = record->meta.displayName;
            record->meta.importSettingsJson = settings.dump(2);
            CopyToBuffer(record->meta.importSettingsJson, importSettingsBuffer.data(), importSettingsBuffer.size());
        }

        ImGui::SeparatorText("Import Settings JSON");
        if (ImGui::InputTextMultiline("##ImportSettingsJson", importSettingsBuffer.data(), importSettingsBuffer.size(), ImVec2(-1.0f, 160.0f))) {
            record->meta.importSettingsJson = importSettingsBuffer.data();
            dirty = true;
        }

        if (ImGui::Button("Save Meta")) {
            record->meta.displayName = displayNameBuffer.data();
            record->displayName = record->meta.displayName;
            record->meta.importSettingsJson = importSettingsBuffer.data();
            if (assetDatabase.WriteMeta(*record)) {
                record->importOutdated = true;
                record->lastImportMessage = "[AssetDatabase] Meta saved; reimport required";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reimport")) {
            assetDatabase.ImportAsset(record->guid);
        }

        if (record->type == AssetType::Texture) {
            ImGui::SeparatorText("Preview");
            if (record->meta.artifacts.empty()) {
                ImGui::TextDisabled("Texture preview waits for an imported DDS artifact");
            } else {
                ImGui::Text("Texture2D artifact: %s", record->meta.artifacts.front().path.c_str());
            }
        } else if (record->type == AssetType::Sky) {
            ImGui::SeparatorText("Preview");
            ImGui::TextDisabled("Cubemap face/equirect preview is reserved for the next preview pass");
        } else if (record->type == AssetType::Model) {
            ImGui::SeparatorText("Preview");
            ImGui::TextDisabled("Model preview scene is reserved for a later pass");
        }
#else
        (void)assetDatabase;
        (void)selection;
#endif
    }

} // namespace HIKARI
