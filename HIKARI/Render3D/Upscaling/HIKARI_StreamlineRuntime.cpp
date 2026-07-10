#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/HIKARI_Math3D.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl.h>
#include <sl_dlss.h>
#include <sl_helpers.h>
#include <sl_security.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING {

    namespace {
        constexpr const char* kStreamlineSdkVersion = "2.12.0";

        struct StreamlineState {
            GFX::Context context{};
            RenderTarget2D output{};
            DXGI_FORMAT outputFormat = DXGI_FORMAT_UNKNOWN;
            uint32_t outputWidth = 0;
            uint32_t outputHeight = 0;
            StreamlineDebugStats stats{};
#if defined(HIKARI_WITH_STREAMLINE)
            sl::FrameToken* frameToken = nullptr;
            sl::ViewportHandle viewport{ 0u };
            bool optionsConfigured = false;
            StreamlineDlssMode configuredMode = StreamlineDlssMode::Off;
            uint32_t configuredOutputWidth = 0;
            uint32_t configuredOutputHeight = 0;
            bool optimalSettingsCached = false;
            StreamlineDlssMode optimalSettingsMode = StreamlineDlssMode::Off;
            uint32_t optimalSettingsOutputWidth = 0;
            uint32_t optimalSettingsOutputHeight = 0;
            StreamlineOptimalSettings optimalSettings{};
            std::wstring pluginPath{};
            std::wstring logPath{};
#endif
        };

        StreamlineState& State() {
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
        std::filesystem::path ModuleDirectory() {
            std::array<wchar_t, 32768> path{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                path.data(),
                static_cast<DWORD>(path.size()));
            if (length == 0 ||
                length >= static_cast<DWORD>(path.size())) {
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

        bool RecordResult(
            StreamlineState& state,
            const char* operation,
            sl::Result result,
            bool countFailure = true) {

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

        sl::DLSSMode ToSlDlssMode(StreamlineDlssMode mode) {
            switch (mode) {
            case StreamlineDlssMode::Dlaa: return sl::DLSSMode::eDLAA;
            case StreamlineDlssMode::Quality: return sl::DLSSMode::eMaxQuality;
            case StreamlineDlssMode::Balanced: return sl::DLSSMode::eBalanced;
            case StreamlineDlssMode::Performance: return sl::DLSSMode::eMaxPerformance;
            case StreamlineDlssMode::UltraPerformance:
                return sl::DLSSMode::eUltraPerformance;
            case StreamlineDlssMode::Off:
            default:
                return sl::DLSSMode::eOff;
            }
        }

        sl::DLSSOptions BuildDlssOptions(
            StreamlineDlssMode mode,
            uint32_t outputWidth,
            uint32_t outputHeight) {

            sl::DLSSOptions options{};
            options.mode = ToSlDlssMode(mode);
            options.outputWidth = outputWidth;
            options.outputHeight = outputHeight;
            options.preExposure = 1.0f;
            options.exposureScale = 1.0f;
            options.colorBuffersHDR = sl::Boolean::eTrue;
            options.useAutoExposure = sl::Boolean::eFalse;
            options.alphaUpscalingEnabled = sl::Boolean::eFalse;
            options.dlaaPreset = sl::DLSSPreset::ePresetK;
            options.qualityPreset = sl::DLSSPreset::ePresetK;
            options.balancedPreset = sl::DLSSPreset::ePresetK;
            options.performancePreset = sl::DLSSPreset::ePresetM;
            options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
            return options;
        }

        sl::float4x4 ToStreamlineMatrix(const MATH::Mat4& matrix) {
            // HIKARI stores column-vector matrices as m[column][row]. Streamline
            // consumes row-vector matrices, so the storage can be copied by the
            // first index to provide the required mathematical transpose.
            sl::float4x4 result{};
            for (uint32_t row = 0; row < 4; ++row) {
                result[row] = sl::float4(
                    matrix.m[row][0],
                    matrix.m[row][1],
                    matrix.m[row][2],
                    matrix.m[row][3]);
            }
            return result;
        }

        sl::Constants BuildConstants(const TEMPORAL::TemporalFrameState& frame) {
            sl::Constants constants{};
            const TEMPORAL::TemporalCameraData& camera = frame.camera;
            constants.cameraViewToClip = ToStreamlineMatrix(camera.proj);
            constants.clipToCameraView =
                ToStreamlineMatrix(MATH::Inverse(camera.proj));

            const MATH::Mat4 clipToPrev =
                camera.previousValid && !frame.resetHistory
                    ? camera.prevUnjitteredViewProj * camera.invUnjitteredViewProj
                    : MATH::Mat4::Identity();
            constants.clipToPrevClip = ToStreamlineMatrix(clipToPrev);
            constants.prevClipToClip =
                ToStreamlineMatrix(MATH::Inverse(clipToPrev));

            constants.jitterOffset = sl::float2(camera.jitter.x, camera.jitter.y);
            constants.mvecScale = sl::float2(
                1.0f / static_cast<float>((std::max)(1u, frame.renderWidth)),
                1.0f / static_cast<float>((std::max)(1u, frame.renderHeight)));
            constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);

            const MATH::Mat4 invView = MATH::Inverse(camera.view);
            const MATH::Vec3 right = MATH::Normalize({
                invView.m[0][0], invView.m[0][1], invView.m[0][2] });
            const MATH::Vec3 up = MATH::Normalize({
                invView.m[1][0], invView.m[1][1], invView.m[1][2] });
            const MATH::Vec3 forward = MATH::Normalize({
                -invView.m[2][0], -invView.m[2][1], -invView.m[2][2] });
            constants.cameraPos = sl::float3(
                camera.cameraPos.x,
                camera.cameraPos.y,
                camera.cameraPos.z);
            constants.cameraRight = sl::float3(right.x, right.y, right.z);
            constants.cameraUp = sl::float3(up.x, up.y, up.z);
            constants.cameraFwd = sl::float3(forward.x, forward.y, forward.z);
            constants.cameraNear = camera.nearZ;
            constants.cameraFar = camera.farZ;
            constants.cameraFOV = camera.fovYRad;
            constants.cameraAspectRatio = camera.aspect;
            constants.depthInverted = sl::Boolean::eFalse;
            constants.cameraMotionIncluded = sl::Boolean::eTrue;
            constants.motionVectors3D = sl::Boolean::eFalse;
            constants.reset = frame.resetHistory
                ? sl::Boolean::eTrue
                : sl::Boolean::eFalse;
            constants.orthographicProjection = sl::Boolean::eFalse;
            constants.motionVectorsDilated = sl::Boolean::eFalse;
            constants.motionVectorsJittered = sl::Boolean::eFalse;
            return constants;
        }

        bool EnsureDlssOutput(
            StreamlineState& state,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format) {

            if (state.context.device == nullptr || state.context.cmdList == nullptr) {
                return false;
            }

            const uint32_t safeWidth = (std::max)(1u, width);
            const uint32_t safeHeight = (std::max)(1u, height);
            state.output.UpdateContext(state.context);
            const bool recreate =
                state.output.GetResource() == nullptr ||
                state.outputWidth != safeWidth ||
                state.outputHeight != safeHeight ||
                state.outputFormat != format;
            if (recreate) {
                state.output.Finalize();
                state.output.UpdateContext(state.context);
                state.output.SetDebugName("Streamline.DLSS.Output");
                if (!state.output.Init(
                        static_cast<int>(safeWidth),
                        static_cast<int>(safeHeight),
                        format,
                        false,
                        { 0.0f, 0.0f, 0.0f, 0.0f },
                        false,
                        true)) {
                    state.stats.outputReady = false;
                    return false;
                }
                state.outputWidth = safeWidth;
                state.outputHeight = safeHeight;
                state.outputFormat = format;
                state.optionsConfigured = false;
            }
            state.stats.outputReady = state.output.GetResource() != nullptr;
            return state.stats.outputReady;
        }

        sl::Resource MakeResource(const TEMPORAL::TemporalTextureView& view) {
            sl::Resource resource(
                sl::ResourceType::eTex2d,
                view.resource,
                static_cast<uint32_t>(view.state));
            resource.width = view.width;
            resource.height = view.height;
            resource.nativeFormat = static_cast<uint32_t>(view.format);
            resource.mipLevels = 1;
            resource.arrayLayers = 1;
            return resource;
        }
#endif
    }

    const char* ToString(StreamlineDlssMode mode) {
        switch (mode) {
        case StreamlineDlssMode::Off: return "off";
        case StreamlineDlssMode::Dlaa: return "DLAA";
        case StreamlineDlssMode::Quality: return "DLSS Quality";
        case StreamlineDlssMode::Balanced: return "DLSS Balanced";
        case StreamlineDlssMode::Performance: return "DLSS Performance";
        case StreamlineDlssMode::UltraPerformance: return "DLSS Ultra Performance";
        default: return "unknown";
        }
    }

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
        StreamlineState& state = State();
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
        const std::filesystem::path logDirectory =
            std::filesystem::absolute("logs");
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

        const sl::Result result = slInit(preferences, sl::kSDKVersion);
        if (!RecordResult(state, "slInit", result)) {
            state.stats.status = StreamlineRuntimeStatus::InitializationFailed;
            return false;
        }

        state.stats.initialized = true;
        state.stats.status = StreamlineRuntimeStatus::AwaitingDevice;
        HIKARI_LOG_INFO(
            std::string("[Streamline] initialized SDK ") + kStreamlineSdkVersion +
            " plugins=" + moduleDirectory.string());
        return true;
#endif
    }

    bool AttachStreamlineDevice(ID3D12Device* device) {
        StreamlineState& state = State();
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)device;
        return false;
#else
        if (!state.stats.initialized || device == nullptr) {
            state.stats.status = StreamlineRuntimeStatus::DeviceFailed;
            return false;
        }

        if (!RecordResult(state, "slSetD3DDevice", slSetD3DDevice(device))) {
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
        StreamlineState& state = State();
        state.context = context;
        state.output.UpdateContext(context);
    }

    StreamlineDlssMode ResolveStreamlineDlssMode(
        const RenderQualitySettings& settings) {

        if (IsDlaaAntiAliasingMode(settings.antiAliasingMode)) {
            return StreamlineDlssMode::Dlaa;
        }
        if (!IsDlssAntiAliasingMode(settings.antiAliasingMode)) {
            return StreamlineDlssMode::Off;
        }

        switch (settings.dlssQualityMode) {
        case DlssQualityMode::Quality: return StreamlineDlssMode::Quality;
        case DlssQualityMode::Balanced: return StreamlineDlssMode::Balanced;
        case DlssQualityMode::Performance: return StreamlineDlssMode::Performance;
        case DlssQualityMode::UltraPerformance:
            return StreamlineDlssMode::UltraPerformance;
        default:
            return StreamlineDlssMode::Quality;
        }
    }

    bool IsStreamlineDlssSuperResolutionMode(StreamlineDlssMode mode) {
        return mode == StreamlineDlssMode::Quality ||
            mode == StreamlineDlssMode::Balanced ||
            mode == StreamlineDlssMode::Performance ||
            mode == StreamlineDlssMode::UltraPerformance;
    }

    bool QueryStreamlineDlssOptimalSettings(
        StreamlineDlssMode mode,
        uint32_t outputWidth,
        uint32_t outputHeight,
        StreamlineOptimalSettings& outSettings) {

        outSettings = {};
        StreamlineState& state = State();
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)mode;
        (void)outputWidth;
        (void)outputHeight;
        return false;
#else
        if (mode == StreamlineDlssMode::Off ||
            !IsStreamlineDlssAvailable() ||
            outputWidth == 0 ||
            outputHeight == 0) {
            return false;
        }

        if (state.optimalSettingsCached &&
            state.optimalSettingsMode == mode &&
            state.optimalSettingsOutputWidth == outputWidth &&
            state.optimalSettingsOutputHeight == outputHeight) {
            outSettings = state.optimalSettings;
            return outSettings.valid;
        }

        const sl::DLSSOptions options =
            BuildDlssOptions(mode, outputWidth, outputHeight);
        sl::DLSSOptimalSettings optimal{};
        if (!RecordResult(
                state,
                "slDLSSGetOptimalSettings",
                slDLSSGetOptimalSettings(options, optimal),
                false)) {
            return false;
        }

        outSettings.valid =
            optimal.optimalRenderWidth > 0 &&
            optimal.optimalRenderHeight > 0;
        outSettings.optimalRenderWidth = optimal.optimalRenderWidth;
        outSettings.optimalRenderHeight = optimal.optimalRenderHeight;
        outSettings.minRenderWidth = optimal.renderWidthMin;
        outSettings.minRenderHeight = optimal.renderHeightMin;
        outSettings.maxRenderWidth = optimal.renderWidthMax;
        outSettings.maxRenderHeight = optimal.renderHeightMax;

        state.optimalSettingsCached = true;
        state.optimalSettingsMode = mode;
        state.optimalSettingsOutputWidth = outputWidth;
        state.optimalSettingsOutputHeight = outputHeight;
        state.optimalSettings = outSettings;
        state.stats.optimalRenderWidth = outSettings.optimalRenderWidth;
        state.stats.optimalRenderHeight = outSettings.optimalRenderHeight;
        state.stats.minRenderWidth = outSettings.minRenderWidth;
        state.stats.minRenderHeight = outSettings.minRenderHeight;
        state.stats.maxRenderWidth = outSettings.maxRenderWidth;
        state.stats.maxRenderHeight = outSettings.maxRenderHeight;
        state.stats.lastOperation = "slDLSSGetOptimalSettings";
        state.stats.lastResult = "eOk";
        return outSettings.valid;
#endif
    }

    bool BeginStreamlineFrame(
        const TEMPORAL::TemporalFrameState& frame,
        StreamlineDlssMode mode) {
        StreamlineState& state = State();
        ResetFrameStats(state, frame.frameIndex);
        state.stats.mode = mode;
        state.stats.dlssRequested = mode != StreamlineDlssMode::Off;
        state.stats.renderWidth = frame.renderWidth;
        state.stats.renderHeight = frame.renderHeight;
        state.stats.outputWidth = frame.outputWidth;
        state.stats.outputHeight = frame.outputHeight;
#if !defined(HIKARI_WITH_STREAMLINE)
        return false;
#else
        state.frameToken = nullptr;
        if (mode == StreamlineDlssMode::Off ||
            !IsStreamlineDlssAvailable() ||
            !frame.camera.valid) {
            return false;
        }

        const uint32_t frameIndex = static_cast<uint32_t>(frame.frameIndex);
        if (!RecordResult(
                state,
                "slGetNewFrameToken",
                slGetNewFrameToken(state.frameToken, &frameIndex)) ||
            state.frameToken == nullptr) {
            return false;
        }
        state.stats.frameTokenReady = true;

        const sl::Constants constants = BuildConstants(frame);
        if (!RecordResult(
                state,
                "slSetConstants",
                slSetConstants(constants, *state.frameToken, state.viewport))) {
            return false;
        }
        state.stats.constantsSubmitted = true;
        state.stats.status = StreamlineRuntimeStatus::Ready;
        return true;
#endif
    }

    RenderTarget2D* ExecuteStreamlineDlss(
        const TEMPORAL::TemporalInputs& inputs,
        StreamlineDlssMode mode) {

        StreamlineState& state = State();
        state.stats.dlssRequested = mode != StreamlineDlssMode::Off;
        state.stats.mode = mode;
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)inputs;
        (void)mode;
        return nullptr;
#else
        const bool requiredInputsReady =
            inputs.sceneColor.valid &&
            inputs.sceneDepth.valid &&
            inputs.motionVectors.valid &&
            inputs.exposure.valid &&
            inputs.reactiveMask.valid &&
            inputs.transparencyMask.valid &&
            inputs.invalidDepthMotionMask.valid;
        StreamlineOptimalSettings optimal{};
        const bool optimalSettingsReady =
            QueryStreamlineDlssOptimalSettings(
                mode,
                inputs.frame.outputWidth,
                inputs.frame.outputHeight,
                optimal);
        const bool renderSizeValid =
            mode == StreamlineDlssMode::Dlaa
                ? inputs.frame.renderWidth == inputs.frame.outputWidth &&
                    inputs.frame.renderHeight == inputs.frame.outputHeight
                : IsStreamlineDlssSuperResolutionMode(mode) &&
                    optimalSettingsReady &&
                    inputs.frame.renderWidth >= optimal.minRenderWidth &&
                    inputs.frame.renderHeight >= optimal.minRenderHeight &&
                    inputs.frame.renderWidth <= optimal.maxRenderWidth &&
                    inputs.frame.renderHeight <= optimal.maxRenderHeight;
        const bool inputExtentsValid =
            inputs.sceneColor.width == inputs.frame.renderWidth &&
            inputs.sceneColor.height == inputs.frame.renderHeight &&
            inputs.sceneDepth.width == inputs.frame.renderWidth &&
            inputs.sceneDepth.height == inputs.frame.renderHeight &&
            inputs.motionVectors.width == inputs.frame.renderWidth &&
            inputs.motionVectors.height == inputs.frame.renderHeight;
        if (!IsStreamlineDlssAvailable() ||
            !state.stats.frameTokenReady ||
            !state.stats.constantsSubmitted ||
            state.frameToken == nullptr ||
            state.context.cmdList == nullptr ||
            !requiredInputsReady ||
            inputs.frame.frameIndex != state.stats.frameIndex ||
            !renderSizeValid ||
            !inputExtentsValid) {
            state.stats.lastOperation = "validate DLSS inputs";
            state.stats.lastResult = "incomplete or mismatched temporal contract";
            ++state.stats.failureCount;
            return nullptr;
        }

        if (!EnsureDlssOutput(
                state,
                inputs.frame.outputWidth,
                inputs.frame.outputHeight,
                inputs.sceneColor.format)) {
            state.stats.lastOperation = "create DLSS output";
            state.stats.lastResult = "UAV render target creation failed";
            ++state.stats.failureCount;
            return nullptr;
        }

        if (!state.optionsConfigured ||
            state.configuredMode != mode ||
            state.configuredOutputWidth != inputs.frame.outputWidth ||
            state.configuredOutputHeight != inputs.frame.outputHeight) {
            const sl::DLSSOptions options = BuildDlssOptions(
                mode,
                inputs.frame.outputWidth,
                inputs.frame.outputHeight);
            if (!RecordResult(
                    state,
                    "slDLSSSetOptions",
                    slDLSSSetOptions(state.viewport, options))) {
                return nullptr;
            }
            state.optionsConfigured = true;
            state.configuredMode = mode;
            state.configuredOutputWidth = inputs.frame.outputWidth;
            state.configuredOutputHeight = inputs.frame.outputHeight;
        }

        state.output.TransitionColor(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        TEMPORAL::TemporalTextureView outputView{};
        outputView.resource = state.output.GetResource();
        outputView.srv = state.output.GetSrvGpu();
        outputView.format = state.output.GetFormat();
        outputView.width = static_cast<uint32_t>(state.output.GetWidth());
        outputView.height = static_cast<uint32_t>(state.output.GetHeight());
        outputView.state = state.output.GetColorState();
        outputView.valid = outputView.resource != nullptr;

        sl::Resource colorInput = MakeResource(inputs.sceneColor);
        sl::Resource colorOutput = MakeResource(outputView);
        sl::Resource depth = MakeResource(inputs.sceneDepth);
        sl::Resource motion = MakeResource(inputs.motionVectors);
        sl::Resource exposure = MakeResource(inputs.exposure);
        sl::Resource reactive = MakeResource(inputs.reactiveMask);
        sl::Resource transparency = MakeResource(inputs.transparencyMask);
        sl::Resource invalidDepthMotion =
            MakeResource(inputs.invalidDepthMotionMask);

        const sl::Extent renderExtent{
            0u, 0u, inputs.frame.renderWidth, inputs.frame.renderHeight };
        const sl::Extent outputExtent{
            0u, 0u, inputs.frame.outputWidth, inputs.frame.outputHeight };
        const sl::Extent exposureExtent{ 0u, 0u, 1u, 1u };
        const sl::ResourceLifecycle lifecycle =
            sl::ResourceLifecycle::eValidUntilEvaluate;
        const std::array<sl::ResourceTag, 8> tags = {
            sl::ResourceTag(&colorInput, sl::kBufferTypeScalingInputColor, lifecycle, &renderExtent),
            sl::ResourceTag(&colorOutput, sl::kBufferTypeScalingOutputColor, lifecycle, &outputExtent),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, lifecycle, &renderExtent),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, lifecycle, &renderExtent),
            sl::ResourceTag(&exposure, sl::kBufferTypeExposure, lifecycle, &exposureExtent),
            sl::ResourceTag(&reactive, sl::kBufferTypeReactiveMaskHint, lifecycle, &renderExtent),
            sl::ResourceTag(&transparency, sl::kBufferTypeTransparencyAndCompositionMaskHint, lifecycle, &renderExtent),
            sl::ResourceTag(&invalidDepthMotion, sl::kBufferTypeInvalidDepthMotionHint, lifecycle, &renderExtent),
        };

        ID3D12GraphicsCommandList* cmd = state.context.cmdList;
        GFX::PIX::ScopedGpuEvent pix(
            cmd,
            GFX::PIX::kColorRender,
            mode == StreamlineDlssMode::Dlaa
                ? "Temporal.Streamline.DLAA"
                : "Temporal.Streamline.DLSS");
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            cmd,
            mode == StreamlineDlssMode::Dlaa
                ? GFX::GPU_PROFILE::Pass::TemporalDlaaResolve
                : GFX::GPU_PROFILE::Pass::TemporalDlssResolve);

        const sl::Result tagResult = slSetTagForFrame(
            *state.frameToken,
            state.viewport,
            tags.data(),
            static_cast<uint32_t>(tags.size()),
            reinterpret_cast<sl::CommandBuffer*>(cmd));
        if (!RecordResult(state, "slSetTagForFrame(DLSS)", tagResult)) {
            state.output.TransitionColor(
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            return nullptr;
        }

        const sl::BaseStructure* evaluateInputs[] = { &state.viewport };
        const sl::Result evaluateResult = slEvaluateFeature(
            sl::kFeatureDLSS,
            *state.frameToken,
            evaluateInputs,
            static_cast<uint32_t>(std::size(evaluateInputs)),
            reinterpret_cast<sl::CommandBuffer*>(cmd));
        state.output.TransitionColor(
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (!RecordResult(
                state,
                "slEvaluateFeature(DLSS)",
                evaluateResult)) {
            GFX::DumpD3D12InfoQueue(
                state.context.device,
                "Streamline DLSS evaluate failed");
            return nullptr;
        }

        state.stats.dlssEvaluated = true;
        ++state.stats.evaluationCount;
        state.stats.status = StreamlineRuntimeStatus::Ready;
        state.stats.lastOperation = "slEvaluateFeature(DLSS)";
        state.stats.lastResult = "eOk";

        if (state.stats.evaluationCount == 1u ||
            (state.stats.evaluationCount % 120u) == 0u) {
            sl::DLSSState dlssState{};
            if (slDLSSGetState(state.viewport, dlssState) == sl::Result::eOk) {
                state.stats.estimatedVramBytes =
                    dlssState.estimatedVRAMUsageInBytes;
            }
        }
        return &state.output;
#endif
    }

    void MarkStreamlineDlssFallback() {
        State().stats.fallbackUsed = true;
    }

    void ShutdownStreamline() {
        StreamlineState& state = State();
        state.output.Finalize();
        state.outputFormat = DXGI_FORMAT_UNKNOWN;
        state.outputWidth = 0;
        state.outputHeight = 0;
#if defined(HIKARI_WITH_STREAMLINE)
        state.frameToken = nullptr;
        if (state.stats.initialized) {
            (void)RecordResult(state, "slShutdown", slShutdown(), false);
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
        const StreamlineDebugStats& stats = State().stats;
        return
            stats.sdkCompiled &&
            stats.initialized &&
            stats.deviceAttached &&
            stats.dlssSupported;
    }

    const StreamlineDebugStats& GetStreamlineDebugStats() {
        return State().stats;
    }

} // namespace HIKARI::RENDER3D::UPSCALING
