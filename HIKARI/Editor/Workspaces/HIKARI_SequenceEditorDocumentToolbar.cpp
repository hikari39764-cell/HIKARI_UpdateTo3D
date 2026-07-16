#include "Editor/Workspaces/HIKARI_SequenceEditorDocumentToolbar.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Documents/HIKARI_SequenceEditorDocumentController.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    SequenceEditorDocumentToolbarResult
        SequenceEditorDocumentToolbar::Draw(
            SequenceEditorDocumentController& controller,
            AssetDatabase& assetDatabase,
            const CinematicSequence* activeSequence,
            const std::string& sceneDisplayName,
            bool libraryVisible,
            bool actionsAllowed) const {

        SequenceEditorDocumentToolbarResult result{};
#if defined(HIKARI_WITH_EDITOR)
        SequenceEditorDocument& document = controller.GetDocument();
        if (ImGui::Button(libraryVisible
                ? "Hide Library"
                : "Show Library")) {
            result.toggleLibraryRequested = true;
        }
        ImGui::SameLine();
        const char* sourceLabel = "Embedded Scene";
        if (document.GetSourceKind() == SequenceEditorSourceKind::Asset) {
            sourceLabel = "Sequence Asset";
        } else if (document.GetSourceKind() ==
                SequenceEditorSourceKind::TransientAsset) {
            sourceLabel = "Unsaved Sequence Asset";
        }
        ImGui::TextDisabled("Source");
        ImGui::SameLine();
        ImGui::TextUnformatted(sourceLabel);
        ImGui::SameLine();
        std::string documentName = activeSequence != nullptr
            ? activeSequence->name
            : document.GetDisplayName();
        if (document.IsDirty()) {
            documentName += " *";
        }
        ImGui::Text("|  %s", documentName.c_str());

        if (!actionsAllowed) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("New")) {
            (void)controller.RequestNewAsset(
                assetDatabase,
                result.statusMessage);
        }
        ImGui::SameLine();
        if (activeSequence == nullptr) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Save")) {
            if (document.IsEmbeddedScene()) {
                result.saveEmbeddedSceneRequested = true;
            } else {
                (void)controller.Save(
                    assetDatabase,
                    result.statusMessage);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As")) {
            (void)controller.SaveAs(
                assetDatabase,
                *activeSequence,
                result.statusMessage);
        }
        if (activeSequence == nullptr) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        const bool canRevert = document.GetSourceKind() ==
            SequenceEditorSourceKind::Asset;
        if (!canRevert) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Revert")) {
            (void)controller.RequestRevert(
                assetDatabase,
                result.statusMessage);
        }
        if (!canRevert) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (!document.GetAssetGuid().IsValid()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Reveal in Assets")) {
            result.revealAssetRequested = true;
            result.revealAssetGuid = document.GetAssetGuid();
            result.revealAssetPath = document.GetSourcePath();
            result.statusMessage = "Sequence selected in Assets";
        }
        if (!document.GetAssetGuid().IsValid()) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (!document.IsExternalDocument()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Close")) {
            (void)controller.RequestCloseToEmbedded(
                assetDatabase,
                result.statusMessage);
        }
        if (!document.IsExternalDocument()) {
            ImGui::EndDisabled();
        }
        if (!actionsAllowed) {
            ImGui::EndDisabled();
        }

        if (document.IsEmbeddedScene()) {
            ImGui::TextDisabled(
                "Scene: %s  |  Saved with the scene document",
                sceneDisplayName.c_str());
        } else if (document.GetSourceKind() ==
                SequenceEditorSourceKind::TransientAsset) {
            ImGui::TextDisabled(
                "Not saved yet. Save will create Assets/Sequences/*.hsequence");
        } else {
            ImGui::TextDisabled(
                "%s  |  GUID %s",
                document.GetSourcePath().generic_string().c_str(),
                document.GetAssetGuid().value.c_str());
        }
#else
        (void)controller;
        (void)assetDatabase;
        (void)activeSequence;
        (void)sceneDisplayName;
        (void)libraryVisible;
        (void)actionsAllowed;
#endif
        return result;
    }

    void SequenceEditorDocumentToolbar::DrawPendingConfirmation(
        SequenceEditorDocumentController& controller,
        AssetDatabase& assetDatabase,
        std::string& inOutStatusMessage) const {

#if defined(HIKARI_WITH_EDITOR)
        if (controller.HasPendingConfirmation() &&
            !ImGui::IsPopupOpen("Unsaved Sequence Asset")) {
            ImGui::OpenPopup("Unsaved Sequence Asset");
        }
        if (!ImGui::BeginPopupModal(
                "Unsaved Sequence Asset",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextWrapped(
            "%s",
            controller.GetPendingConfirmationText());
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            if (controller.ConfirmSaveThenContinue(
                    assetDatabase,
                    inOutStatusMessage)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(100.0f, 0.0f))) {
            (void)controller.ConfirmDiscardAndContinue(
                assetDatabase,
                inOutStatusMessage);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            controller.CancelPending();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
#else
        (void)controller;
        (void)assetDatabase;
        (void)inOutStatusMessage;
#endif
    }

} // namespace HIKARI::EDITOR
