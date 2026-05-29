#include "HIKARI_AssetInspectorPanel.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <Windows.h>
#include <Shellapi.h>
#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Editor/HIKARI_EditorSelection.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        const char* ToAssetTypeText(AssetType type) {
            switch (type) {
            case AssetType::Model: return "Model";
            case AssetType::Scene: return "Scene";
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

        const char* CoordinateSystemItems[] = {
            "RightHanded_YUp", "LeftHanded_YUp", "RightHanded_ZUp"
        };

        const char* GeneratePolicyItems[] = {
            "IfMissing", "Always", "Never"
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

        std::string ReadTextFile(const std::filesystem::path& path) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return {};
            }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            return oss.str();
        }

        bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return false;
            }
            outJson = nlohmann::json::parse(ifs, nullptr, false);
            return !outJson.is_discarded() && outJson.is_object();
        }

        void ShowInExplorer(const std::filesystem::path& path) {
            const std::wstring param = L"/select,\"" + path.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
        }

        void OpenFolder(const std::filesystem::path& path) {
            ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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

        bool DrawBoolSetting(const char* label, nlohmann::json& settings, const char* key, bool fallback) {
            bool value = settings.value(key, fallback);
            if (ImGui::Checkbox(label, &value)) {
                settings[key] = value;
                return true;
            }
            return false;
        }

        bool DrawIntSetting(const char* label, nlohmann::json& settings, const char* key, int fallback, int minimum = 0) {
            int value = settings.value(key, fallback);
            if (ImGui::InputInt(label, &value)) {
                settings[key] = (std::max)(minimum, value);
                return true;
            }
            return false;
        }

        bool DrawFloatSetting(const char* label, nlohmann::json& settings, const char* key, float fallback, float minimum = 0.0f) {
            float value = settings.value(key, fallback);
            if (ImGui::InputFloat(label, &value)) {
                settings[key] = (std::max)(minimum, value);
                return true;
            }
            return false;
        }

        void DrawPathRow(const char* label, const std::filesystem::path& path) {
            ImGui::Text("%s: %s", label, path.generic_string().c_str());
        }

        void DrawModelDiagnostics(const AssetRecord& record) {
            nlohmann::json report;
            if (!ReadJsonFile(record.importedDirectory / "import_report.json", report) ||
                !report.contains("diagnostics") ||
                !report["diagnostics"].is_object()) {
                ImGui::TextDisabled("Model diagnostics are written after a successful HMODEL import");
                return;
            }

            const nlohmann::json& diagnostics = report["diagnostics"];
            if (diagnostics.contains("summary") && diagnostics["summary"].is_object()) {
                const nlohmann::json& summary = diagnostics["summary"];
                ImGui::SeparatorText("HMODEL Summary");
                ImGui::Text("Meshes: %d  Primitives: %d  Materials: %d  Textures: %d",
                    summary.value("meshes", 0),
                    summary.value("primitives", 0),
                    summary.value("materials", 0),
                    summary.value("textures", 0));
                ImGui::Text("Static Vertices: %d  Skinned Vertices: %d  Indices: %d",
                    summary.value("staticVertices", 0),
                    summary.value("skinnedVertices", 0),
                    summary.value("indices", 0));
                ImGui::Text("HTEX Refs: %d  Fallback Textures: %d",
                    summary.value("htexRefs", 0),
                    summary.value("fallbackTextures", 0));
            }

            if (diagnostics.contains("textures") &&
                diagnostics["textures"].is_array() &&
                ImGui::BeginTable("ModelDiagnosticTextures", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                ImGui::TableSetupColumn("HTEX", ImGuiTableColumnFlags_WidthFixed, 54.0f);
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Runtime");
                ImGui::TableHeadersRow();

                for (const nlohmann::json& texture : diagnostics["textures"]) {
                    if (!texture.is_object()) {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", texture.value("index", -1));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(texture.value("htexReady", false) ? "Yes" : "No");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(texture.value("sourcePath", "").c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(texture.value("runtimePath", "").c_str());
                }

                ImGui::EndTable();
            }

            if (diagnostics.contains("materials") &&
                diagnostics["materials"].is_array() &&
                ImGui::BeginTable("ModelDiagnosticMaterials", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Material");
                ImGui::TableSetupColumn("Metallic", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn("Roughness", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Alpha", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableHeadersRow();

                for (const nlohmann::json& material : diagnostics["materials"]) {
                    if (!material.is_object()) {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(material.value("name", "<unnamed>").c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", material.value("metallicFactor", 0.0f));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", material.value("roughnessFactor", 1.0f));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(material.value("alphaMode", "Opaque").c_str());
                }

                ImGui::EndTable();
            }
        }

        bool DrawMaterialAssetEditor(
            AssetDatabase& assetDatabase,
            AssetRegistry& assetRegistry,
            EditorSelection& selection,
            AssetRecord& record,
            AssetGuid& outApplyRuntimeGuid,
            PbrMaterialAssetData& outApplyRuntimeData,
            std::string& outRefreshRuntimeGuid) {

            static std::string loadedMaterialGuid{};
            static PbrMaterialAssetData editData{};
            static std::string materialStatus{};
            static bool materialDirty = false;

            const std::filesystem::path sourcePath =
                (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
            if (loadedMaterialGuid != record.guid.value) {
                loadedMaterialGuid = record.guid.value;
                std::string error{};
                if (!LoadPbrMaterialAssetData(sourcePath, editData, error)) {
                    editData = {};
                    editData.materialName = record.displayName.empty()
                        ? record.sourcePath.stem().string()
                        : record.displayName;
                    materialStatus = error;
                } else {
                    materialStatus.clear();
                }
                materialDirty = false;
            }

            bool changed = false;
            char nameBuffer[256]{};
            CopyToBuffer(editData.materialName, nameBuffer, sizeof(nameBuffer));
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                editData.materialName = nameBuffer;
                changed = true;
            }
            ImGui::TextDisabled("Shader: PBR");

            if (!materialStatus.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", materialStatus.c_str());
            }

            // HE 風に、よく触る slot だけを開きやすい折り畳みで並べる。
            if (ImGui::CollapsingHeader("Base Color", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed = EDITOR::DrawMaterialTextureSlot(
                    "Albedo",
                    editData.baseColorTexture,
                    assetDatabase,
                    assetRegistry,
                    &selection) || changed;
                changed = ImGui::ColorEdit4("Color", &editData.baseColorFactor.x) || changed;
            }

            if (ImGui::CollapsingHeader("Normal")) {
                changed = EDITOR::DrawMaterialTextureSlot(
                    "Normal",
                    editData.normalTexture,
                    assetDatabase,
                    assetRegistry,
                    &selection) || changed;
                changed = ImGui::DragFloat("Scale", &editData.normalScale, 0.01f, 0.0f, 4.0f) || changed;
            }

            if (ImGui::CollapsingHeader("Metallic / Roughness", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed = EDITOR::DrawMaterialTextureSlot(
                    "MR Texture",
                    editData.metallicRoughnessTexture,
                    assetDatabase,
                    assetRegistry,
                    &selection) || changed;
                changed = ImGui::SliderFloat("Metallic", &editData.metallicFactor, 0.0f, 1.0f) || changed;
                changed = ImGui::SliderFloat("Roughness", &editData.roughnessFactor, 0.0f, 1.0f) || changed;
            }

            if (ImGui::CollapsingHeader("Ambient Occlusion")) {
                changed = EDITOR::DrawMaterialTextureSlot(
                    "Occlusion",
                    editData.occlusionTexture,
                    assetDatabase,
                    assetRegistry,
                    &selection) || changed;
                changed = ImGui::SliderFloat("Strength", &editData.occlusionStrength, 0.0f, 1.0f) || changed;
            }

            if (ImGui::CollapsingHeader("Emissive")) {
                changed = EDITOR::DrawMaterialTextureSlot(
                    "Emissive",
                    editData.emissiveTexture,
                    assetDatabase,
                    assetRegistry,
                    &selection) || changed;
                changed = ImGui::ColorEdit3("Color##Emissive", &editData.emissiveFactor.x) || changed;
                changed = ImGui::DragFloat("Strength##Emissive", &editData.emissiveStrength, 0.01f, 0.0f, 100.0f) || changed;
            }

            if (ImGui::CollapsingHeader("Options")) {
                changed = ImGui::Checkbox("Double Sided", &editData.doubleSided) || changed;
                changed = ImGui::Checkbox("Unlit", &editData.unlit) || changed;
            }

            if (changed) {
                materialDirty = true;
            }

            if (materialDirty) {
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "Unsaved material edits");
            } else {
                ImGui::TextDisabled("Material file is clean");
            }

            if (ImGui::Button("Apply Runtime")) {
                outApplyRuntimeGuid = record.guid;
                outApplyRuntimeData = editData;
                materialStatus = "Applied to runtime preview";
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Material")) {
                std::string error{};
                if (SavePbrMaterialAssetData(sourcePath, editData, error)) {
                    materialDirty = false;
                    materialStatus = "Material saved";
                    assetDatabase.ImportAsset(record.guid);
                    assetDatabase.ScanAssets(true);
                    EDITOR::ClearMaterialTextureSlotPreviewCache();
                    outRefreshRuntimeGuid = record.guid.value;
                } else {
                    materialStatus = error.empty() ? "Material save failed" : error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) {
                std::string error{};
                if (LoadPbrMaterialAssetData(sourcePath, editData, error)) {
                    materialStatus.clear();
                    materialDirty = false;
                    EDITOR::ClearMaterialTextureSlotPreviewCache();
                } else {
                    materialStatus = error;
                }
            }

            return materialDirty;
        }
#endif
    }

    void AssetInspectorPanel::Draw(AssetDatabase& assetDatabase, AssetRegistry& assetRegistry, EditorSelection& selection) const {
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
        static std::array<char, 16384> importSettingsBuffer{};
        if (loadedGuid != record->guid.value) {
            loadedGuid = record->guid.value;
            CopyToBuffer(record->displayName, displayNameBuffer.data(), displayNameBuffer.size());
            CopyToBuffer(record->meta.importSettingsJson, importSettingsBuffer.data(), importSettingsBuffer.size());
        }

        bool dirty = false;
        nlohmann::json settings = ReadSettings(*record);
        const AssetImportState importState = GetImportState(*record);

        auto saveMeta = [&]() {
            record->meta.displayName = displayNameBuffer.data();
            record->displayName = record->meta.displayName;
            record->meta.importSettingsJson = importSettingsBuffer.data();
            if (assetDatabase.WriteMeta(*record)) {
                record->importOutdated = true;
                record->lastImportMessage = "[AssetDatabase] Meta saved; reimport required";
            }
        };

        if (ImGui::BeginTabBar("AssetInspectorTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Summary")) {
                if (ImGui::InputText("Display Name", displayNameBuffer.data(), displayNameBuffer.size())) {
                    dirty = true;
                }

                ImGui::Text("GUID: %s", record->guid.value.c_str());
                ImGui::Text("Type: %s", ToAssetTypeText(record->type));
                DrawPathRow("Source Path", record->sourcePath);
                DrawPathRow("Meta Path", record->metaPath);
                ImGui::Text("Importer: %s", record->meta.importerId.empty() ? "<none>" : record->meta.importerId.c_str());
                ImGui::Text("Importer Version: %u", record->meta.importerVersion);
                DrawPathRow("Imported Directory", record->importedDirectory);
                ImGui::Text("State: %s", ToString(importState));
                if (!record->lastImportMessage.empty()) {
                    ImGui::TextWrapped("Last Import Message: %s", record->lastImportMessage.c_str());
                }

                if (ImGui::Button("Copy GUID")) {
                    ImGui::SetClipboardText(record->guid.value.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Show Source")) {
                    ShowInExplorer(assetDatabase.GetProjectRoot() / record->sourcePath);
                }
                ImGui::SameLine();
                if (ImGui::Button("Show Imported Directory") && !record->importedDirectory.empty()) {
                    OpenFolder(record->importedDirectory);
                }

                if (ImGui::Button("Reimport")) {
                    assetDatabase.ImportAsset(record->guid);
                }
                ImGui::SameLine();
                if (ImGui::Button("Import Dependencies")) {
                    assetDatabase.ImportDependencies(record->guid, false);
                }
                ImGui::SameLine();
                if (ImGui::Button("Save Meta")) {
                    if (dirty) {
                        record->meta.importSettingsJson = settings.dump(2);
                        CopyToBuffer(record->meta.importSettingsJson, importSettingsBuffer.data(), importSettingsBuffer.size());
                    }
                    saveMeta();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Import Settings")) {
                if (record->type == AssetType::Texture) {
                    dirty = DrawComboSetting("Usage", settings, "usage", TextureUsageItems, IM_ARRAYSIZE(TextureUsageItems)) || dirty;
                    dirty = DrawComboSetting("Color Space", settings, "colorSpace", TextureColorSpaceItems, IM_ARRAYSIZE(TextureColorSpaceItems)) || dirty;
                    dirty = DrawComboSetting("Compression", settings, "compression", TextureCompressionItems, IM_ARRAYSIZE(TextureCompressionItems)) || dirty;
                    dirty = DrawComboSetting("Mip Policy", settings, "mipPolicy", TextureMipPolicyItems, IM_ARRAYSIZE(TextureMipPolicyItems)) || dirty;
                    dirty = DrawIntSetting("Max Size", settings, "maxSize", 4096, 1) || dirty;
                    dirty = DrawBoolSetting("Force Power Of Two", settings, "forcePowerOfTwo", false) || dirty;
                    dirty = DrawBoolSetting("Allow Resize", settings, "allowResize", false) || dirty;
                } else if (record->type == AssetType::Sky) {
                    dirty = DrawBoolSetting("Copy Sky Cubemap", settings, "copySkyCubemap", true) || dirty;
                    dirty = DrawBoolSetting("Auto Bake IBL", settings, "autoBakeIBL", false) || dirty;
                    dirty = DrawIntSetting("Irradiance Size", settings, "irradianceSize", 64, 1) || dirty;
                    dirty = DrawIntSetting("Prefiltered Size", settings, "prefilteredSize", 256, 1) || dirty;
                    dirty = DrawIntSetting("Prefiltered Mip Count", settings, "prefilteredMipCount", 7, 0) || dirty;
                } else if (record->type == AssetType::Model) {
                    dirty = DrawFloatSetting("Unit Scale", settings, "unitScale", 1.0f, 0.0f) || dirty;
                    dirty = DrawComboSetting("Coordinate System", settings, "coordinateSystem", CoordinateSystemItems, IM_ARRAYSIZE(CoordinateSystemItems)) || dirty;
                    dirty = DrawComboSetting("Generate Normals", settings, "generateNormals", GeneratePolicyItems, IM_ARRAYSIZE(GeneratePolicyItems)) || dirty;
                    dirty = DrawComboSetting("Generate Tangents", settings, "generateTangents", GeneratePolicyItems, IM_ARRAYSIZE(GeneratePolicyItems)) || dirty;
                    dirty = DrawBoolSetting("Load Materials", settings, "loadMaterials", true) || dirty;
                    dirty = DrawBoolSetting("Load Textures", settings, "loadTextures", true) || dirty;
                } else if (record->type == AssetType::Scene) {
                    dirty = DrawBoolSetting("Cook Scene", settings, "cookScene", false) || dirty;
                    ImGui::TextDisabled("Scene cook is reserved; SceneSerializer JSON remains the runtime source.");
                } else {
                    ImGui::TextDisabled("No editable import settings for this asset type yet");
                }

                if (dirty) {
                    record->meta.displayName = displayNameBuffer.data();
                    record->displayName = record->meta.displayName;
                    record->meta.importSettingsJson = settings.dump(2);
                    CopyToBuffer(record->meta.importSettingsJson, importSettingsBuffer.data(), importSettingsBuffer.size());
                }
                if (ImGui::Button("Save Meta")) {
                    saveMeta();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reimport")) {
                    assetDatabase.ImportAsset(record->guid);
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Artifacts")) {
                if (record->meta.artifacts.empty()) {
                    ImGui::TextDisabled("No artifacts");
                } else if (ImGui::BeginTable("AssetArtifactsTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn("Role");
                    ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Exists", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("Path");
                    ImGui::TableHeadersRow();

                    for (const AssetArtifactDesc& artifact : record->meta.artifacts) {
                        const std::filesystem::path artifactPath = assetDatabase.GetProjectRoot() / artifact.path;
                        const bool exists = std::filesystem::exists(artifactPath);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(artifact.role.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(artifact.format.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(exists ? "Yes" : "No");
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(artifact.path.c_str());
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Open Folder")) {
                                OpenFolder(artifactPath.parent_path());
                            }
                            if (ImGui::MenuItem("Copy Path")) {
                                ImGui::SetClipboardText(artifact.path.c_str());
                            }
                            ImGui::EndPopup();
                        }
                    }

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Dependencies")) {
                if (record->meta.dependencies.empty()) {
                    ImGui::TextDisabled("No dependencies");
                } else if (ImGui::BeginTable("AssetDependenciesTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn("Role");
                    ImGui::TableSetupColumn("GUID");
                    ImGui::TableSetupColumn("Resolved", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Path");
                    ImGui::TableHeadersRow();

                    for (const AssetDependencyDesc& dependency : record->meta.dependencies) {
                        const bool resolved = dependency.guid.IsValid()
                            ? assetDatabase.FindByGuid(dependency.guid) != nullptr
                            : !dependency.path.empty() && std::filesystem::exists(assetDatabase.GetProjectRoot() / dependency.path);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(dependency.role.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(dependency.guid.value.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(resolved ? "Yes" : "No");
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(dependency.path.c_str());
                    }

                    ImGui::EndTable();
                }
                if (!record->meta.dependencies.empty()) {
                    if (ImGui::Button("Import Dependencies")) {
                        assetDatabase.ImportDependencies(record->guid, false);
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Preview")) {
                if (record->type == AssetType::Texture) {
                    if (record->meta.artifacts.empty()) {
                        ImGui::TextDisabled("Texture preview waits for an imported DDS artifact");
                    } else {
                        ImGui::Text("Texture artifact: %s", record->meta.artifacts.front().path.c_str());
                        ImGui::TextDisabled("SRV preview loading is reserved for the texture preview pass");
                    }
                } else if (record->type == AssetType::Sky) {
                    ImGui::TextDisabled("Cubemap face/equirect preview is reserved for the sky preview pass");
                    for (const AssetArtifactDesc& artifact : record->meta.artifacts) {
                        if (artifact.role == "SkyCubemap") {
                            ImGui::Text("Sky cubemap: %s", artifact.path.c_str());
                        }
                    }
                } else if (record->type == AssetType::Model) {
                    DrawPathRow("Model Source", record->sourcePath);
                    DrawModelDiagnostics(*record);
                } else if (record->type == AssetType::Scene) {
                    DrawPathRow("Scene Source", record->sourcePath);
                    ImGui::TextDisabled("Scene is managed as a project asset; runtime scene switching still uses the scene catalog.");
                } else {
                    ImGui::TextDisabled("No preview for this asset type yet");
                }
                ImGui::EndTabItem();
            }

            if (record->type == AssetType::Material && ImGui::BeginTabItem("PBR Material")) {
                DrawMaterialAssetEditor(
                    assetDatabase,
                    assetRegistry,
                    selection,
                    *record,
                    applyRuntimeMaterialGuid_,
                    applyRuntimeMaterialData_,
                    refreshRuntimeMaterialGuid_);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Debug JSON")) {
                if (ImGui::InputTextMultiline(
                    "Import Settings JSON",
                    importSettingsBuffer.data(),
                    importSettingsBuffer.size(),
                    ImVec2(-1.0f, 160.0f))) {
                    record->meta.importSettingsJson = importSettingsBuffer.data();
                    dirty = true;
                }
                if (ImGui::Button("Save Raw Import Settings")) {
                    saveMeta();
                }

                ImGui::SeparatorText("Raw Meta JSON");
                const std::string metaText = ReadTextFile(record->metaPath);
                if (metaText.empty()) {
                    ImGui::TextDisabled("Meta JSON is not available");
                } else {
                    ImGui::InputTextMultiline(
                        "##RawMetaJson",
                        const_cast<char*>(metaText.c_str()),
                        metaText.size() + 1,
                        ImVec2(-1.0f, 180.0f),
                        ImGuiInputTextFlags_ReadOnly);
                }

                ImGui::SeparatorText("Import Report JSON");
                const std::string reportText = ReadTextFile(record->importedDirectory / "import_report.json");
                if (reportText.empty()) {
                    ImGui::TextDisabled("Import report is not available");
                } else {
                    ImGui::InputTextMultiline(
                        "##ImportReportJson",
                        const_cast<char*>(reportText.c_str()),
                        reportText.size() + 1,
                        ImVec2(-1.0f, 180.0f),
                        ImGuiInputTextFlags_ReadOnly);
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        if (dirty) {
            record->meta.displayName = displayNameBuffer.data();
            record->displayName = record->meta.displayName;
        }
#else
        (void)assetDatabase;
        (void)assetRegistry;
        (void)selection;
#endif
    }

    bool AssetInspectorPanel::ConsumeApplyRuntimeMaterialRequest(
        AssetGuid& outGuid,
        PbrMaterialAssetData& outData) const {

        if (!applyRuntimeMaterialGuid_.IsValid()) {
            return false;
        }

        outGuid = applyRuntimeMaterialGuid_;
        outData = applyRuntimeMaterialData_;
        applyRuntimeMaterialGuid_ = {};
        applyRuntimeMaterialData_ = {};
        return true;
    }

    std::string AssetInspectorPanel::ConsumeRefreshRuntimeMaterialGuid() const {
        std::string guid = std::move(refreshRuntimeMaterialGuid_);
        refreshRuntimeMaterialGuid_.clear();
        return guid;
    }

} // namespace HIKARI
