#include "Editor/Workspaces/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <utility>

#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
        constexpr const char* kUnsavedDocumentPopup =
            "Unsaved Animation State Machine";
    }

    void AnimationStateMachineWorkspaceController::SelectEntryState()
        noexcept {
        selectionKind_ = SelectionKind::State;
        selectedStateId_ = document_.Definition().entryStateId;
        selectedParameterId_ = {};
        selectedTransitionId_ = {};
        NormalizeSelection();
    }

    void AnimationStateMachineWorkspaceController::RequestDocumentAction(
        DocumentSceneBase& scene,
        AnimationStateMachineWorkspaceResult& result,
        GraphMenuRequests& requests,
        PendingDocumentAction action,
        AssetGuid assetGuid) {
        if (action == PendingDocumentAction::None) return;
        if (!document_.IsDirty()) {
            ExecuteDocumentAction(
                scene, result, requests, action, assetGuid);
            return;
        }

        pendingDocumentAction_ = action;
        pendingDocumentAssetGuid_ = std::move(assetGuid);
        requestUnsavedDocumentDialog_ = true;
    }

    void AnimationStateMachineWorkspaceController::ExecuteDocumentAction(
        DocumentSceneBase& scene,
        AnimationStateMachineWorkspaceResult& result,
        GraphMenuRequests& requests,
        PendingDocumentAction action,
        const AssetGuid& assetGuid) {
        switch (action) {
        case PendingDocumentAction::NewDocument:
            document_.New();
            SelectEntryState();
            requests.resetView = true;
            statusMessage_ = "New animation state machine";
            break;
        case PendingDocumentAction::OpenAsset:
            if (assetGuid.IsValid() && document_.Open(
                    scene.GetAssetDatabase(), assetGuid, statusMessage_)) {
                SelectEntryState();
                requests.frameAll = true;
            }
            break;
        case PendingDocumentAction::ExitToScene:
            result.exitToSceneRequested = true;
            break;
        case PendingDocumentAction::None:
            break;
        }

        pendingDocumentAction_ = PendingDocumentAction::None;
        pendingDocumentAssetGuid_ = {};
    }

    void AnimationStateMachineWorkspaceController::DrawUnsavedDocumentDialog(
        DocumentSceneBase& scene,
        AnimationStateMachineWorkspaceResult& result,
        GraphMenuRequests& requests) {
#if defined(HIKARI_WITH_EDITOR)
        if (requestUnsavedDocumentDialog_) {
            ImGui::OpenPopup(kUnsavedDocumentPopup);
            requestUnsavedDocumentDialog_ = false;
        }

        if (!ImGui::BeginPopupModal(
                kUnsavedDocumentPopup,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::TextUnformatted(
            "The current state machine has unsaved changes.");
        ImGui::TextDisabled(
            "Save them before continuing, or discard them.");
        ImGui::Spacing();

        if (ImGui::Button("Save and Continue")) {
            if (Save(scene, statusMessage_)) {
                ExecuteDocumentAction(
                    scene,
                    result,
                    requests,
                    pendingDocumentAction_,
                    pendingDocumentAssetGuid_);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and Continue")) {
            ExecuteDocumentAction(
                scene,
                result,
                requests,
                pendingDocumentAction_,
                pendingDocumentAssetGuid_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pendingDocumentAction_ = PendingDocumentAction::None;
            pendingDocumentAssetGuid_ = {};
            statusMessage_ = "Document action cancelled";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
#else
        (void)scene;
        (void)result;
        (void)requests;
#endif
    }

} // namespace HIKARI::EDITOR
