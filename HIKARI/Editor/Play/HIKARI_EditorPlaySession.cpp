#include "Editor/Play/HIKARI_EditorPlaySession.h"

#include <utility>

#include "Core/HIKARI_TimeService.h"
#include "HIKARI_Services.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

    EditorPlaySession::~EditorPlaySession() {
        standaloneSession_.Stop();
    }
	// エディタ内でのプレイを開始するリクエストを処理します。プレイセッションの状態を更新し、必要に応じてプレイモードをInProcessに設定します。
    void EditorPlaySession::RequestInProcessStart() {
        if (IsRunning() || state_ == EditorPlayState::Starting) {
            return;
        }
        pendingAction_ = PendingAction::StartInProcess;
        statusMessage_ = "Play in New Window requested.";
    }
	// スタンドアロンモードでのプレイを開始するリクエストを処理します。プロジェクトのルートパスと起動シーンのGUIDを受け取り、プレイセッションの状態を更新します。
    void EditorPlaySession::RequestStandaloneStart(
        const std::filesystem::path& projectRoot,
        const std::string& startupSceneGuid) {
        if (IsRunning() || state_ == EditorPlayState::Starting) {
            return;
        }
        pendingProjectRoot_ = projectRoot;
        pendingSceneGuid_ = startupSceneGuid;
        pendingAction_ = PendingAction::StartStandalone;
        statusMessage_ = "Standalone Game requested.";
    }
	// プレイセッションの停止をリクエストします。停止理由を指定し、プレイセッションの状態を更新します。すでに停止中または停止要求中の場合は何も行いません。
    void EditorPlaySession::RequestStop(PlayStopReason reason) {
        if (mode_ == EditorPlayMode::None &&
            state_ != EditorPlayState::Starting) {
            return;
        }
        if (state_ == EditorPlayState::StopRequested ||
            state_ == EditorPlayState::Draining ||
            state_ == EditorPlayState::RestoringEditor) {
            return;
        }
        stopReason_ = reason;
        state_ = EditorPlayState::StopRequested;
        statusMessage_ = "Stopping Play...";
    }
	//  プレイセッションの状態を更新します。シーンを引数として受け取り、現在のプレイモードに応じて適切な処理を行います
    void EditorPlaySession::Update(DocumentSceneBase& scene) {
        UpdateStandaloneState(scene);

        if (IsInProcessRunning() &&
            SERVICES::ConsumeInProcessGameCloseRequest()) {
            RequestStop(PlayStopReason::WindowClose);
        }

        if (state_ == EditorPlayState::StopRequested ||
            state_ == EditorPlayState::Draining ||
            state_ == EditorPlayState::RestoringEditor) {
            if (mode_ == EditorPlayMode::InProcess) {
                AdvanceInProcessStop(scene);
            } else if (mode_ == EditorPlayMode::Standalone) {
                StopStandalone(scene);
            }
            return;
        }

        const PendingAction action = pendingAction_;
        pendingAction_ = PendingAction::None;
        switch (action) {
        case PendingAction::StartInProcess:
            StartInProcess(scene);
            break;
        case PendingAction::StartStandalone:
            StartStandalone(scene);
            break;
        case PendingAction::None:
        default:
            break;
        }
    }
	// プレイセッションをシャットダウンします。シーンを引数として受け取り、現在のプレイモードに応じて適切な処理を行い、プレイセッションの状態をリセットします。
    void EditorPlaySession::Shutdown(DocumentSceneBase* scene) {
        pendingAction_ = PendingAction::None;
        if (mode_ == EditorPlayMode::InProcess) {
            (void)SERVICES::EndInProcessGamePresentation();
            if (scene != nullptr) {
                (void)scene->EndRuntimePlay();
            }
        }
        standaloneSession_.Stop();
        if (scene != nullptr) {
            (void)RestoreEditorAfterStandalone(*scene);
        }
        mode_ = EditorPlayMode::None;
        state_ = EditorPlayState::Stopped;
    }
	// スタンドアロンモードのプレイセッションが終了するまで待機します。タイムアウト時間をミリ秒単位で指定し、終了した場合はtrue、タイムアウトした場合はfalseを返します。
    bool EditorPlaySession::WaitForStandaloneExit(
        uint32_t timeoutMilliseconds) const {
        if (!IsStandaloneRunning()) {
            return true;
        }
        return standaloneSession_.WaitForExit(timeoutMilliseconds);
    }

    bool EditorPlaySession::IsRunning() const {
        return state_ == EditorPlayState::Running;
    }

    bool EditorPlaySession::IsInProcessRunning() const {
        return mode_ == EditorPlayMode::InProcess && IsRunning();
    }

    bool EditorPlaySession::IsStandaloneRunning() const {
        return mode_ == EditorPlayMode::Standalone && IsRunning();
    }

    bool EditorPlaySession::IsTransitioning() const {
        return state_ == EditorPlayState::Starting ||
            state_ == EditorPlayState::StopRequested ||
            state_ == EditorPlayState::Draining ||
            state_ == EditorPlayState::RestoringEditor;
    }

    void EditorPlaySession::StartInProcess(DocumentSceneBase& scene) {
        state_ = EditorPlayState::Starting;
        mode_ = EditorPlayMode::InProcess;
        if (!scene.BeginRuntimePlay()) {
            Fail("Could not create the runtime scene for Play.");
            return;
        }
        if (!SERVICES::BeginInProcessGamePresentation()) {
            (void)scene.EndRuntimePlay();
            Fail("Could not transfer presentation to the game window.");
            return;
        }

        state_ = EditorPlayState::Running;
        statusMessage_ = "Play in New Window is running.";
    }

    void EditorPlaySession::StartStandalone(DocumentSceneBase& scene) {
        state_ = EditorPlayState::Starting;
        mode_ = EditorPlayMode::Standalone;
        if (!SERVICES::ParkEditorForStandalone(scene)) {
            Fail("Could not park editor resources for Standalone Game.");
            return;
        }
        editorParkedForStandalone_ = true;
        if (!standaloneSession_.Launch(
                pendingProjectRoot_,
                pendingSceneGuid_)) {
            const std::string error = standaloneSession_.GetStatusMessage();
            (void)RestoreEditorAfterStandalone(scene);
            Fail(error);
            return;
        }

        pendingProjectRoot_.clear();
        pendingSceneGuid_.clear();
        state_ = EditorPlayState::Running;
        statusMessage_ = standaloneSession_.GetStatusMessage();
    }
	// In-processプレイセッションの停止を進めます。シーンを引数として受け取り、現在の停止状態に応じて適切な処理を行います。プレイセッションの状態を更新し、必要に応じてエディタのプレゼンテーションを復元します。
    void EditorPlaySession::AdvanceInProcessStop(DocumentSceneBase& scene) {
        if (state_ == EditorPlayState::StopRequested) {
            if (!SERVICES::BeginInProcessGamePresentationStop()) {
                state_ = EditorPlayState::Running;
                statusMessage_ = "Could not begin the Play shutdown sequence.";
                return;
            }
            state_ = EditorPlayState::Draining;
            statusMessage_ = "Stopping Play: draining frame generation...";
            return;
        }

        if (state_ == EditorPlayState::Draining) {
            if (!SERVICES::DrainInProcessGamePresentation()) {
                state_ = EditorPlayState::Running;
                statusMessage_ = "Could not drain the game presentation surface.";
                return;
            }
            state_ = EditorPlayState::RestoringEditor;
            statusMessage_ = "Stopping Play: restoring editor presentation...";
            return;
        }

        if (state_ != EditorPlayState::RestoringEditor) {
            return;
        }

        const bool presentationRestored =
            SERVICES::RestoreEditorPresentation();
        if (SERVICES::IsInProcessGamePresentationActive()) {
            statusMessage_ = "Editor presentation restore is still pending.";
            return;
        }

        const bool sceneRestored = scene.EndRuntimePlay();
        TIME::ResetFrameClock();
        mode_ = EditorPlayMode::None;
        if (!presentationRestored || !sceneRestored) {
            state_ = EditorPlayState::Failed;
            statusMessage_ = !sceneRestored
                ? "Play stopped, but the editor scene could not be restored."
                : "Play stopped, but DLSS-G did not unload cleanly.";
            return;
        }
        state_ = EditorPlayState::Stopped;
        statusMessage_ = "Play stopped.";
    }

    void EditorPlaySession::StopStandalone(DocumentSceneBase& scene) {
        standaloneSession_.Stop();
        const bool restored = RestoreEditorAfterStandalone(scene);
        mode_ = EditorPlayMode::None;
        state_ = restored
            ? EditorPlayState::Stopped
            : EditorPlayState::Failed;
        statusMessage_ = restored
            ? "Standalone Game stopped."
            : "Standalone Game stopped, but the editor scene restore failed.";
    }

    void EditorPlaySession::UpdateStandaloneState(DocumentSceneBase& scene) {
        if (mode_ != EditorPlayMode::Standalone ||
            state_ != EditorPlayState::Running) {
            return;
        }

        standaloneSession_.Update();
        if (standaloneSession_.IsRunning()) {
            return;
        }

        const GamePreviewState standaloneState = standaloneSession_.GetState();
        statusMessage_ = standaloneSession_.GetStatusMessage();
        const bool restored = RestoreEditorAfterStandalone(scene);
        mode_ = EditorPlayMode::None;
        state_ = standaloneState == GamePreviewState::Failed || !restored
            ? EditorPlayState::Failed
            : EditorPlayState::Stopped;
        if (!restored) {
            statusMessage_ += " Editor scene restore failed.";
        }
    }

    bool EditorPlaySession::RestoreEditorAfterStandalone(
        DocumentSceneBase& scene) {
        if (!editorParkedForStandalone_) {
            return true;
        }
        const bool restored = SERVICES::RestoreEditorAfterStandalone(scene);
        TIME::ResetFrameClock();
        editorParkedForStandalone_ = false;
        return restored;
    }

    void EditorPlaySession::Fail(std::string message) {
        pendingProjectRoot_.clear();
        pendingSceneGuid_.clear();
        mode_ = EditorPlayMode::None;
        state_ = EditorPlayState::Failed;
        statusMessage_ = std::move(message);
    }

} // namespace HIKARI::EDITOR
