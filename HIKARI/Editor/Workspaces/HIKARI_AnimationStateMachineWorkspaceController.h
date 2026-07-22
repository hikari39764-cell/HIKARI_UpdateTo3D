#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Documents/HIKARI_AnimationStateMachineEditorDocument.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI { class DocumentSceneBase; }
namespace HIKARI::ANIMATION {
    struct AnimationStateMachineRuntimeSnapshot;
}

namespace HIKARI::EDITOR {

    class EditorWorkspaceHost;

    struct AnimationStateMachineWorkspaceResult {
        bool exitToSceneRequested = false;
        std::string statusMessage{};
    };

    class AnimationStateMachineWorkspaceController {
    public:
        void ApplyWorkspaceActivation(
            DocumentSceneBase& scene,
            const EditorWorkspaceActivation& activation);
        void DrawDockSpace(bool resetDefaultDockLayout) const;
        AnimationStateMachineWorkspaceResult Draw(
            DocumentSceneBase& scene);

        bool IsDocumentOpen() const noexcept;
        bool IsDocumentDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        bool Save(DocumentSceneBase& scene, std::string& outMessage);
        bool Undo(std::string& outMessage);
        bool Redo(std::string& outMessage);

    private:
        enum class SelectionKind : uint8_t {
            None,
            State,
            Parameter,
            Transition,
        };

        struct GraphMenuRequests {
            bool addStateAtCenter = false;
            bool frameAll = false;
            bool frameSelection = false;
            bool resetView = false;
        };

        enum class PendingDocumentAction : uint8_t {
            None,
            NewDocument,
            OpenAsset,
            ExitToScene,
        };

        void DrawGraphWindow(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result);
        void DrawGraphMenuBar(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests);
        void RequestDocumentAction(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests,
            PendingDocumentAction action,
            AssetGuid assetGuid = {});
        void ExecuteDocumentAction(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests,
            PendingDocumentAction action,
            const AssetGuid& assetGuid);
        void DrawUnsavedDocumentDialog(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests);
        void SelectEntryState() noexcept;
        void DrawParametersWindow();
        void DrawTransitionsWindow();
        void DrawDetailsWindow(DocumentSceneBase& scene);
        void DrawSelectedStateMotionEditor(
            DocumentSceneBase& scene,
            ANIMATION::AnimationState& state);
        void DrawDiagnosticsWindow(DocumentSceneBase& scene);
        void DrawRuntimeDebug(DocumentSceneBase& scene);
        const ANIMATION::AnimationStateMachineRuntimeSnapshot*
            ResolveRuntimeDebugSnapshot(DocumentSceneBase& scene);
        void AddState();
        void AddStateAt(float graphX, float graphY);
        void DeleteSelectedState();
        void AddParameter(ANIMATION::AnimationParameterType type);
        void DeleteSelectedParameter();
        void AddTransition();
        void AddTransitionBetween(
            ANIMATION::AnimationStateId source,
            ANIMATION::AnimationStateId target);
        void DeleteSelectedTransition();
        void NormalizeSelection() noexcept;
        void RecordMutation(
            ANIMATION::AnimationStateMachineDefinition before,
            uint64_t mergeGroup = 0u);

        ANIMATION::AnimationState* SelectedState() noexcept;
        ANIMATION::AnimationParameterDefinition* SelectedParameter() noexcept;
        ANIMATION::AnimationStateTransition* SelectedTransition() noexcept;

        AnimationStateMachineEditorDocument document_{};
        SelectionKind selectionKind_ = SelectionKind::None;
        ANIMATION::AnimationStateId selectedStateId_{};
        ANIMATION::AnimationParameterId selectedParameterId_{};
        ANIMATION::AnimationTransitionId selectedTransitionId_{};
        float graphPanX_ = 0.0f;
        float graphPanY_ = 0.0f;
        float graphZoom_ = 1.0f;
        ANIMATION::AnimationStateId graphContextStateId_{};
        ANIMATION::AnimationTransitionId graphContextTransitionId_{};
        ANIMATION::AnimationStateId transitionDragSourceStateId_{};
        float graphContextX_ = 0.0f;
        float graphContextY_ = 0.0f;
        std::optional<ANIMATION::AnimationStateMachineDefinition>
            graphDragBefore_{};
        RuntimeObjectHandle runtimeDebugObject_{};
        PendingDocumentAction pendingDocumentAction_ =
            PendingDocumentAction::None;
        AssetGuid pendingDocumentAssetGuid_{};
        bool requestUnsavedDocumentDialog_ = false;
        std::string statusMessage_{};
    };

} // namespace HIKARI::EDITOR
