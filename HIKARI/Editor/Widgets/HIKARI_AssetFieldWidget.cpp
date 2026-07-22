#include "HIKARI_AssetFieldWidget.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Widgets/HIKARI_AssetPickerPopup.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        std::unordered_map<ImGuiID, AssetPickerPopupState> gPickerStates{};

        const char* ToAssetTypeText(AssetType type) {
            switch (type) {
            case AssetType::Model: return "Model";
            case AssetType::Scene: return "Scene";
            case AssetType::Texture: return "Texture";
            case AssetType::Sky: return "Sky";
            case AssetType::Material: return "Material";
            case AssetType::VfxEffect: return "VFX";
            case AssetType::Sequence: return "Sequence";
            case AssetType::AnimationStateMachine:
                return "Animation State Machine";
            default: return "Asset";
            }
        }

        std::string ShortGuid(std::string_view guid) {
            if (guid.size() <= 8) {
                return std::string(guid);
            }
            return std::string(guid.substr(0, 8));
        }

        std::string BuildPreviewText(const AssetRecord* record, const std::string& guid) {
            if (guid.empty()) {
                return "<none>";
            }
            if (!record) {
                return "Missing: " + ShortGuid(guid);
            }
            if (!record->displayName.empty()) {
                return record->displayName;
            }
            return record->sourcePath.stem().string();
        }

        bool AcceptAssetGuidPayload(const AssetDatabase* assetDatabase, AssetType requiredType, std::string& inOutGuid) {
            if (!assetDatabase) {
                return false;
            }

            DroppedAssetPayload payload{};
            const bool accepted = requiredType == AssetType::Unknown
                ? AcceptAssetDrop(*assetDatabase, payload)
                : AcceptAssetDropOfType(*assetDatabase, requiredType, payload);
            if (!accepted || !payload.record) {
                return false;
            }

            inOutGuid = payload.guid.value;
            return true;
        }
#endif
    }

    bool DrawAssetField(
        const AssetDatabase* assetDatabase,
        const AssetFieldOptions& options,
        std::string& inOutGuid,
        EditorSelection* selection) {
#if defined(HIKARI_WITH_EDITOR)
        const std::string labelText(options.label.empty() ? ToAssetTypeText(options.requiredType) : std::string(options.label));
        if (!assetDatabase) {
            std::array<char, 256> buffer{};
            strncpy_s(buffer.data(), buffer.size(), inOutGuid.c_str(), buffer.size() - 1);
            if (ImGui::InputText(labelText.c_str(), buffer.data(), buffer.size())) {
                inOutGuid = buffer.data();
                return true;
            }
            return false;
        }

        bool changed = false;
        const AssetRecord* current = inOutGuid.empty() ? nullptr : assetDatabase->FindByGuid(AssetGuid{ inOutGuid });
        const std::string popupId = "AssetPickerPopup##" + labelText;
        const std::string preview = BuildPreviewText(current, inOutGuid);

        ImGui::PushID(labelText.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(labelText.c_str());
        ImGui::SameLine();
        const float buttonWidth = (std::max)(160.0f, ImGui::GetContentRegionAvail().x - 176.0f);
        if (ImGui::Button(preview.c_str(), ImVec2(buttonWidth, 0.0f))) {
            ImGui::OpenPopup(popupId.c_str());
        }
        changed = AcceptAssetGuidPayload(assetDatabase, options.requiredType, inOutGuid) || changed;

        ImGui::SameLine();
        if (ImGui::SmallButton("Select")) {
            ImGui::OpenPopup(popupId.c_str());
        }

        if (options.allowClear) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                inOutGuid.clear();
                changed = true;
            }
        }

        if (options.allowLocate) {
            ImGui::SameLine();
            const bool canLocate = current != nullptr && selection != nullptr;
            if (!canLocate) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Locate") && canLocate) {
                selection->selectedAssetGuid = current->guid.value;
                selection->selectedAssetPath = current->sourcePath.generic_string();
                selection->selectedAsset = nullptr;
            }
            if (!canLocate) {
                ImGui::EndDisabled();
            }
        }

        if (options.allowCopyGuid) {
            ImGui::SameLine();
            const bool canCopy = !inOutGuid.empty();
            if (!canCopy) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Copy") && canCopy) {
                ImGui::SetClipboardText(inOutGuid.c_str());
            }
            if (!canCopy) {
                ImGui::EndDisabled();
            }
        }

        const ImGuiID fieldId = ImGui::GetID("AssetFieldState");
        AssetPickerPopupState& popupState = gPickerStates[fieldId];
        changed = DrawAssetPickerPopup(popupId.c_str(), *assetDatabase, options.requiredType, inOutGuid, popupState) || changed;

        if (!inOutGuid.empty()) {
            if (current) {
                const AssetImportState state = GetImportState(*current);
                ImGui::TextDisabled("%s | %s | %s",
                    current->sourcePath.generic_string().c_str(),
                    ToString(state),
                    ShortGuid(current->guid.value).c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Missing %s: %s", ToAssetTypeText(options.requiredType), inOutGuid.c_str());
            }
        } else {
            ImGui::TextDisabled("No %s selected", ToAssetTypeText(options.requiredType));
        }

        ImGui::PopID();
        return changed;
#else
        (void)assetDatabase;
        (void)options;
        (void)inOutGuid;
        (void)selection;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
