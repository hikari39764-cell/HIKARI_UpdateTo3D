#include "Editor/Play/HIKARI_GamePreviewSession.h"

#include <Windows.h>

#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Runtime/HIKARI_RuntimeHost.h"

namespace HIKARI::EDITOR {

    namespace {
        std::filesystem::path CurrentExecutablePath() {
            std::vector<wchar_t> buffer(32768u, L'\0');
            const DWORD length = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) {
                return {};
            }
            return std::filesystem::path(buffer.data());
        }

        std::wstring QuoteArgument(const std::filesystem::path& value) {
            return L"\"" + value.wstring() + L"\"";
        }

        bool WritePreviewConfig(
            const std::filesystem::path& path,
            const std::filesystem::path& projectRoot,
            const std::string& startupSceneGuid,
            std::string& errorMessage) {
            const RENDER3D::RenderQualitySettings& quality =
                RENDER3D::GetRenderQualitySettings();
            RENDER3D::RenderResolution windowResolution =
                RENDER3D::ResolveFixedRenderResolution(quality.windowSize);
            if (windowResolution.width <= 0 || windowResolution.height <= 0) {
                windowResolution = { 1280, 720 };
            }

            const nlohmann::json runtime{
                { "hostMode", RuntimeHostModeName(RuntimeHostMode::GamePreview) },
                { "projectRoot", projectRoot.generic_string() },
                { "enableImGui", false },
                { "enableEditorUI", false },
                { "enablePortableObjectTools", false },
                { "enableDebugLayer", false },
                { "enableDebugCamera", false },
                { "resizableWindow",
                    quality.windowMode ==
                    RENDER3D::WindowPresentationMode::Windowed },
                { "windowWidth", windowResolution.width },
                { "windowHeight", windowResolution.height },
                { "renderQuality",
                    RENDER3D::SerializeRenderQualitySettings(quality) },
                { "startupSceneGuid", startupSceneGuid },
            };
            const nlohmann::json root{ { "runtime", runtime } };

            std::error_code ec{};
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                errorMessage =
                    "Could not create Game Preview config directory: " +
                    path.parent_path().string();
                return false;
            }

            std::ofstream output(path);
            if (!output) {
                errorMessage =
                    "Could not write Game Preview config: " + path.string();
                return false;
            }
            output << root.dump(4);
            if (!output.good()) {
                errorMessage =
                    "Could not finish writing Game Preview config: " +
                    path.string();
                return false;
            }
            return true;
        }

        HANDLE CreatePreviewJob() {
            HANDLE job = CreateJobObjectW(nullptr, nullptr);
            if (job == nullptr) {
                return nullptr;
            }

            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(
                    job,
                    JobObjectExtendedLimitInformation,
                    &limits,
                    sizeof(limits))) {
                CloseHandle(job);
                return nullptr;
            }
            return job;
        }
    }

    GamePreviewSession::~GamePreviewSession() {
        Stop();
    }

    bool GamePreviewSession::Launch(
        const std::filesystem::path& projectRoot,
        const std::string& startupSceneGuid) {
        Stop();
        if (projectRoot.empty() || startupSceneGuid.empty()) {
            Fail("Game Preview requires a saved scene asset.");
            return false;
        }

        const std::filesystem::path executable = CurrentExecutablePath();
        std::error_code ec{};
        if (!std::filesystem::is_regular_file(executable, ec) || ec) {
            Fail("Could not locate the editor executable for Game Preview.");
            return false;
        }

        configPath_ =
            projectRoot / "Library" / "Temp" / "GamePreview" /
            ("runtime_config_" + std::to_string(GetCurrentProcessId()) + ".json");
        std::string configError{};
        if (!WritePreviewConfig(
                configPath_,
                projectRoot,
                startupSceneGuid,
                configError)) {
            Fail(std::move(configError));
            return false;
        }

        std::wstring commandLine =
            QuoteArgument(executable) + L" --runtime-config " +
            QuoteArgument(configPath_);
        std::vector<wchar_t> mutableCommandLine(
            commandLine.begin(),
            commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        HANDLE job = CreatePreviewJob();
        if (!CreateProcessW(
                executable.c_str(),
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_SUSPENDED,
                nullptr,
                projectRoot.c_str(),
                &startupInfo,
                &processInfo)) {
            const DWORD error = GetLastError();
            if (job != nullptr) {
                CloseHandle(job);
            }
            Fail(
                "Could not launch Game Preview. Windows error " +
                std::to_string(error) + ".");
            return false;
        }

        if (job != nullptr && !AssignProcessToJobObject(job, processInfo.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
        if (ResumeThread(processInfo.hThread) == static_cast<DWORD>(-1)) {
            const DWORD error = GetLastError();
            TerminateProcess(processInfo.hProcess, 1u);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            if (job != nullptr) {
                CloseHandle(job);
            }
            Fail(
                "Could not start the Game Preview process. Windows error " +
                std::to_string(error) + ".");
            return false;
        }

        CloseHandle(processInfo.hThread);
        processHandle_ = processInfo.hProcess;
        jobHandle_ = job;
        processId_ = processInfo.dwProcessId;
        state_ = GamePreviewState::Running;
        statusMessage_ =
            "Game Preview running (PID " + std::to_string(processId_) + ")";
        return true;
    }

    void GamePreviewSession::Stop() {
        HANDLE process = static_cast<HANDLE>(processHandle_);
        HANDLE job = static_cast<HANDLE>(jobHandle_);
        if (process != nullptr) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(process, &exitCode) &&
                exitCode == STILL_ACTIVE) {
                if (job != nullptr) {
                    TerminateJobObject(job, 0u);
                } else {
                    TerminateProcess(process, 0u);
                }
                WaitForSingleObject(process, 1000u);
            }
        }
        CloseHandles();
        if (state_ == GamePreviewState::Running) {
            statusMessage_ = "Game Preview stopped.";
        }
        state_ = GamePreviewState::Stopped;
    }

    void GamePreviewSession::Update() {
        if (state_ != GamePreviewState::Running || processHandle_ == nullptr) {
            return;
        }

        DWORD exitCode = 0;
        HANDLE process = static_cast<HANDLE>(processHandle_);
        if (!GetExitCodeProcess(process, &exitCode)) {
            const DWORD error = GetLastError();
            Fail(
                "Could not query the Game Preview process. Windows error " +
                std::to_string(error) + ".");
            return;
        }
        if (exitCode == STILL_ACTIVE) {
            return;
        }

        CloseHandles();
        processId_ = 0;
        state_ = exitCode == 0
            ? GamePreviewState::Stopped
            : GamePreviewState::Failed;
        statusMessage_ =
            "Game Preview exited with code " + std::to_string(exitCode) + ".";
    }

    bool GamePreviewSession::WaitForExit(uint32_t timeoutMilliseconds) const {
        if (processHandle_ == nullptr) {
            return true;
        }
        return WaitForSingleObject(
            static_cast<HANDLE>(processHandle_),
            timeoutMilliseconds) == WAIT_OBJECT_0;
    }

    void GamePreviewSession::CloseHandles() {
        if (processHandle_ != nullptr) {
            CloseHandle(static_cast<HANDLE>(processHandle_));
            processHandle_ = nullptr;
        }
        if (jobHandle_ != nullptr) {
            CloseHandle(static_cast<HANDLE>(jobHandle_));
            jobHandle_ = nullptr;
        }
        processId_ = 0;

        if (!configPath_.empty()) {
            std::error_code ec{};
            std::filesystem::remove(configPath_, ec);
            configPath_.clear();
        }
    }

    void GamePreviewSession::Fail(std::string message) {
        CloseHandles();
        state_ = GamePreviewState::Failed;
        statusMessage_ = std::move(message);
    }

} // namespace HIKARI::EDITOR
