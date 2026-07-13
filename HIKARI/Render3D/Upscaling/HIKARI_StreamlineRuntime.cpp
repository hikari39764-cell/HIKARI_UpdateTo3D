#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#include <array>
#include <filesystem>
#include <iterator>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Render3D/Upscaling/HIKARI_StreamlineInternal.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl_helpers.h>
#include <sl_security.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING {

    namespace {
#if defined(HIKARI_WITH_STREAMLINE)
        std::filesystem::path ModuleDirectory() {
            std::array<wchar_t, 32768> path{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                path.data(),
                static_cast<DWORD>(path.size()));
            if (length == 0 || length >= static_cast<DWORD>(path.size())) {
                return {};
            }
            return std::filesystem::path(path.data()).parent_path();
        }

        void StreamlineLogCallback(sl::LogType type, const char* message) {
            if (message == nullptr || message[0] == '\0') {
                return;
            }
            const std::string text = std::string("[Streamline] ") + message;
            if (type == sl::LogType::eError) {
                HIKARI_LOG_ERROR(text);
            } else if (type == sl::LogType::eWarn) {
                HIKARI_LOG_WARN(text);
            }
        }
#endif
    }

    namespace INTERNAL {

        StreamlineState& GetState() {
            static StreamlineState state{};
            static const bool initialized = [&] {
#if defined(HIKARI_WITH_STREAMLINE)
                state.stats.sdkCompiled = true;
                state.stats.status = StreamlineRuntimeStatus::AwaitingInitialization;
                state.stats.sdkVersion = kStreamlineSdkVersion;
#else
                state.stats.status = StreamlineRuntimeStatus::NotCompiled;
#endif
                return true;
            }();
            (void)initialized;
            return state;
        }

        void ResetFrameStats(StreamlineState& state, uint64_t frameIndex) {
            state.stats.frameIndex = frameIndex;
            state.stats.frameTokenReady = false;
            state.stats.constantsSubmitted = false;
            state.stats.dlssRequested = false;
            state.stats.dlssEvaluated = false;
            state.stats.fallbackUsed = false;
        }

#if defined(HIKARI_WITH_STREAMLINE)
        bool RecordResult(
            StreamlineState& state,
            const char* operation,
            sl::Result result,
            bool countFailure) {
            state.stats.lastOperation = operation != nullptr ? operation : "Streamline";
            state.stats.lastResult = sl::getResultAsStr(result);
            if (result == sl::Result::eOk) {
                return true;
            }
            if (countFailure) {
                ++state.stats.failureCount;
            }
            state.stats.status = StreamlineRuntimeStatus::RuntimeFailure;
            HIKARI_LOG_WARN(
                std::string("[Streamline] ") + state.stats.lastOperation +
                " failed: " + state.stats.lastResult);
            return false;
        }
#endif

    } // namespace INTERNAL

    const char* ToString(StreamlineRuntimeStatus status) {
        switch (status) {
        case StreamlineRuntimeStatus::NotCompiled: return "not compiled";
        case StreamlineRuntimeStatus::AwaitingInitialization: return "awaiting init";
        case StreamlineRuntimeStatus::SignatureRejected: return "signature rejected";
        case StreamlineRuntimeStatus::InitializationFailed: return "init failed";
        case StreamlineRuntimeStatus::AwaitingDevice: return "awaiting device";
        case StreamlineRuntimeStatus::DeviceFailed: return "device failed";
        case StreamlineRuntimeStatus::FeatureUnsupported: return "unsupported";
        case StreamlineRuntimeStatus::Ready: return "ready";
        case StreamlineRuntimeStatus::RuntimeFailure: return "runtime failure";
        default: return "unknown";
        }
    }

    bool InitializeStreamlineEarly() {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
        state.stats.status = StreamlineRuntimeStatus::NotCompiled;
        return false;
#else
        if (state.stats.initialized) {
            return true;
        }

        const std::filesystem::path moduleDirectory = ModuleDirectory();
        const std::filesystem::path interposerPath =
            moduleDirectory / L"sl.interposer.dll";
        if (moduleDirectory.empty() ||
            !std::filesystem::is_regular_file(interposerPath) ||
            !sl::security::verifyEmbeddedSignature(interposerPath.c_str())) {
            state.stats.status = StreamlineRuntimeStatus::SignatureRejected;
            state.stats.lastOperation = "verify sl.interposer.dll";
            state.stats.lastResult = "signature verification failed";
            HIKARI_LOG_ERROR(
                "[Streamline] signed sl.interposer.dll was not found or failed verification.");
            return false;
        }

        state.pluginPath = moduleDirectory.wstring();
        const std::filesystem::path logDirectory = std::filesystem::absolute("logs");
        std::error_code error{};
        std::filesystem::create_directories(logDirectory, error);
        state.logPath = logDirectory.wstring();

        const wchar_t* pluginPaths[] = { state.pluginPath.c_str() };
        const sl::Feature features[] = { sl::kFeatureDLSS };
        sl::Preferences preferences{};
        preferences.showConsole = false;
        preferences.logLevel = sl::LogLevel::eDefault;
        preferences.pathsToPlugins = pluginPaths;
        preferences.numPathsToPlugins = static_cast<uint32_t>(std::size(pluginPaths));
        preferences.pathToLogsAndData = state.logPath.c_str();
        preferences.logMessageCallback = StreamlineLogCallback;
        preferences.flags =
            sl::PreferenceFlags::eDisableDebugText |
            sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        preferences.featuresToLoad = features;
        preferences.numFeaturesToLoad = static_cast<uint32_t>(std::size(features));
        preferences.engine = sl::EngineType::eCustom;
        preferences.engineVersion = "HIKARI-1";
        preferences.projectId = "e7cc83d6-d200-4d4a-85e4-7d93e060b066";
        preferences.renderAPI = sl::RenderAPI::eD3D12;

        if (!INTERNAL::RecordResult(
                state,
                "slInit",
                slInit(preferences, sl::kSDKVersion))) {
            state.stats.status = StreamlineRuntimeStatus::InitializationFailed;
            return false;
        }

        state.stats.initialized = true;
        state.stats.status = StreamlineRuntimeStatus::AwaitingDevice;
        HIKARI_LOG_INFO(
            std::string("[Streamline] initialized SDK ") +
            INTERNAL::kStreamlineSdkVersion +
            " plugins=" + moduleDirectory.string());
        return true;
#endif
    }

    bool AttachStreamlineDevice(ID3D12Device* device) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)device;
        return false;
#else
        if (!state.stats.initialized || device == nullptr) {
            state.stats.status = StreamlineRuntimeStatus::DeviceFailed;
            return false;
        }
        if (!INTERNAL::RecordResult(state, "slSetD3DDevice", slSetD3DDevice(device))) {
            state.stats.status = StreamlineRuntimeStatus::DeviceFailed;
            return false;
        }
        state.stats.deviceAttached = true;

        LUID luid = device->GetAdapterLuid();
        sl::AdapterInfo adapter{};
        adapter.deviceLUID = reinterpret_cast<uint8_t*>(&luid);
        adapter.deviceLUIDSizeInBytes = sizeof(luid);
        const sl::Result support = slIsFeatureSupported(sl::kFeatureDLSS, adapter);
        state.stats.lastOperation = "slIsFeatureSupported(DLSS)";
        state.stats.lastResult = sl::getResultAsStr(support);
        if (support != sl::Result::eOk) {
            state.stats.dlssSupported = false;
            state.stats.status = StreamlineRuntimeStatus::FeatureUnsupported;
            HIKARI_LOG_WARN(
                std::string("[Streamline] DLSS/DLAA unavailable: ") +
                state.stats.lastResult);
            return false;
        }

        state.stats.dlssSupported = true;
        state.stats.status = StreamlineRuntimeStatus::Ready;
        HIKARI_LOG_INFO("[Streamline] DLSS/DLAA supported on the active D3D12 device.");
        return true;
#endif
    }

    void UpdateStreamlineContext(const GFX::Context& context) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        state.context = context;
        state.output.UpdateContext(context);
    }

    void ShutdownStreamline() {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        state.output.Finalize();
        state.outputFormat = DXGI_FORMAT_UNKNOWN;
        state.outputWidth = 0;
        state.outputHeight = 0;
#if defined(HIKARI_WITH_STREAMLINE)
        state.frameToken = nullptr;
        if (state.stats.initialized) {
            (void)INTERNAL::RecordResult(state, "slShutdown", slShutdown(), false);
        }
        state.stats.initialized = false;
        state.stats.deviceAttached = false;
        state.stats.dlssSupported = false;
        state.stats.frameTokenReady = false;
        state.stats.constantsSubmitted = false;
        state.optionsConfigured = false;
        state.configuredMode = StreamlineDlssMode::Off;
        state.optimalSettingsCached = false;
        state.stats.status = StreamlineRuntimeStatus::AwaitingInitialization;
#endif
    }

    bool IsStreamlineDlssAvailable() {
        const StreamlineDebugStats& stats = INTERNAL::GetState().stats;
        return stats.sdkCompiled &&
            stats.initialized &&
            stats.deviceAttached &&
            stats.dlssSupported;
    }

    const StreamlineDebugStats& GetStreamlineDebugStats() {
        return INTERNAL::GetState().stats;
    }

} // namespace HIKARI::RENDER3D::UPSCALING
