#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Editor/Play/HIKARI_GamePreviewSession.h"

namespace HIKARI {

    class DocumentSceneBase;

    namespace EDITOR {

        enum class EditorPlayMode {
            None,
            InProcess,
            Standalone,
        };

        enum class EditorPlayState {
            Stopped,
            Starting,
            Running,
            StopRequested,
            Draining,
            RestoringEditor,
            Failed,
        };

        enum class PlayStopReason {
            Toolbar,
            WindowClose,
            Escape,
            ApplicationShutdown,
        };

        class EditorPlaySession {
        public:
            EditorPlaySession() = default;
            ~EditorPlaySession();

            EditorPlaySession(const EditorPlaySession&) = delete;
            EditorPlaySession& operator=(const EditorPlaySession&) = delete;

            void RequestInProcessStart();
            void RequestStandaloneStart(
                const std::filesystem::path& projectRoot,
                const std::string& startupSceneGuid);
            void RequestStop(
                PlayStopReason reason = PlayStopReason::Toolbar);
            void Update(DocumentSceneBase& scene);
            void Shutdown(DocumentSceneBase* scene);

            bool WaitForStandaloneExit(uint32_t timeoutMilliseconds) const;

            EditorPlayMode GetMode() const { return mode_; }
            EditorPlayState GetState() const { return state_; }
            bool IsRunning() const;
            bool IsInProcessRunning() const;
            bool IsStandaloneRunning() const;
            bool IsTransitioning() const;
            const std::string& GetStatusMessage() const { return statusMessage_; }

        private:
            enum class PendingAction {
                None,
                StartInProcess,
                StartStandalone,
            };

            void StartInProcess(DocumentSceneBase& scene);
            void StartStandalone(DocumentSceneBase& scene);
            void AdvanceInProcessStop(DocumentSceneBase& scene);
            void StopStandalone(DocumentSceneBase& scene);
            void UpdateStandaloneState(DocumentSceneBase& scene);
            bool RestoreEditorAfterStandalone(DocumentSceneBase& scene);
            void Fail(std::string message);

            GamePreviewSession standaloneSession_{};
            PendingAction pendingAction_ = PendingAction::None;
            EditorPlayMode mode_ = EditorPlayMode::None;
            EditorPlayState state_ = EditorPlayState::Stopped;
            std::filesystem::path pendingProjectRoot_{};
            std::string pendingSceneGuid_{};
            std::string statusMessage_{};
            PlayStopReason stopReason_ = PlayStopReason::Toolbar;
            bool editorParkedForStandalone_ = false;
        };

    } // namespace EDITOR
} // namespace HIKARI
