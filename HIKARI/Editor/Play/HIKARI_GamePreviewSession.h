#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace HIKARI::EDITOR {

    enum class GamePreviewState {
        Stopped,
        Running,
        Failed,
    };

    class GamePreviewSession {
    public:
        GamePreviewSession() = default;
        ~GamePreviewSession();

        GamePreviewSession(const GamePreviewSession&) = delete;
        GamePreviewSession& operator=(const GamePreviewSession&) = delete;

        bool Launch(
            const std::filesystem::path& projectRoot,
            const std::string& startupSceneGuid);
        void Stop();
        void Update();
        bool WaitForExit(uint32_t timeoutMilliseconds) const;

        GamePreviewState GetState() const { return state_; }
        bool IsRunning() const { return state_ == GamePreviewState::Running; }
        uint32_t GetProcessId() const { return processId_; }
        const std::string& GetStatusMessage() const { return statusMessage_; }

    private:
        void CloseHandles();
        void Fail(std::string message);

        void* processHandle_ = nullptr;
        void* jobHandle_ = nullptr;
        uint32_t processId_ = 0;
        GamePreviewState state_ = GamePreviewState::Stopped;
        std::filesystem::path configPath_{};
        std::string statusMessage_{};
    };

} // namespace HIKARI::EDITOR
