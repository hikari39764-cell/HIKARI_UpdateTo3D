#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::TOOLS::BAKING {

    enum class LightingBakeTarget {
        All,
        ReflectionProbesOnly,
        LightProbesOnly,
        LightmapsOnly,
    };

    enum class LightingBakeAction {
        PrepareManifest,
        ClearBake,
        BakeReflectionProbes,
        ValidateOnly,
    };

    enum class LightingBakeJobState {
        Idle,
        Requested,
        Capturing,
        WaitingGpu,
        Finalizing,
        Completed,
        Failed,
    };

    struct LightingBakeReport {
        bool success = true;
        bool manifestWritten = false;
        bool bakeFolderCreated = false;
        bool bakeFolderCleared = false;
        bool reflectionProbeCaptured = false;
        bool reflectionProbePrefiltered = false;
        bool reflectionProbeRecordWritten = false;
        bool reflectionProbeSceneCaptured = false;
        bool reflectionProbeUsedSourceOverride = false;
        bool reflectionProbeCaptureValidated = false;
        bool reflectionProbePrefilterValidated = false;
        LightingBakeAction action = LightingBakeAction::ValidateOnly;
        LightingBakeTarget target = LightingBakeTarget::All;
        LightingBakeJobState jobState = LightingBakeJobState::Idle;
        std::filesystem::path bakeRoot{};
        std::filesystem::path manifestPath{};
        std::filesystem::path reflectionProbeCapturePath{};
        std::filesystem::path reflectionProbePrefilteredPath{};
        std::filesystem::path reflectionProbeBrdfLutPath{};
        std::string reflectionProbeCaptureMode{};
        uint64_t gpuFenceValue = 0;
        uint32_t reflectionProbeCapturedFaceCount = 0;
        uint32_t reflectionProbeQueuedReadbackFaceCount = 0;
        uint32_t reflectionProbeCaptureResolution = 0;
        uint32_t reflectionProbeCaptureMipCount = 0;
        std::string reflectionProbeCaptureFormat{};
        uint32_t reflectionProbePrefilteredMipCount = 0;
        std::string reflectionProbePrefilteredFormat{};
        uint32_t reflectionProbeRecordCount = 0;
        uint32_t lightProbeRecordCount = 0;
        uint32_t lightmapRecordCount = 0;
        std::vector<std::string> reflectionProbeFaceSummaries{};
        std::vector<std::string> messages{};
        std::vector<std::string> warnings{};
        std::vector<std::string> errors{};
    };

    inline const char* ToString(LightingBakeTarget target) {
        switch (target) {
        case LightingBakeTarget::All:
            return "All";
        case LightingBakeTarget::ReflectionProbesOnly:
            return "ReflectionProbesOnly";
        case LightingBakeTarget::LightProbesOnly:
            return "LightProbesOnly";
        case LightingBakeTarget::LightmapsOnly:
            return "LightmapsOnly";
        default:
            return "Unknown";
        }
    }

    inline const char* ToString(LightingBakeAction action) {
        switch (action) {
        case LightingBakeAction::PrepareManifest:
            return "PrepareManifest";
        case LightingBakeAction::ClearBake:
            return "ClearBake";
        case LightingBakeAction::BakeReflectionProbes:
            return "BakeReflectionProbes";
        case LightingBakeAction::ValidateOnly:
            return "ValidateOnly";
        default:
            return "Unknown";
        }
    }

    inline const char* ToString(LightingBakeJobState state) {
        switch (state) {
        case LightingBakeJobState::Idle:
            return "Idle";
        case LightingBakeJobState::Requested:
            return "Requested";
        case LightingBakeJobState::Capturing:
            return "Capturing";
        case LightingBakeJobState::WaitingGpu:
            return "WaitingGpu";
        case LightingBakeJobState::Finalizing:
            return "Finalizing";
        case LightingBakeJobState::Completed:
            return "Completed";
        case LightingBakeJobState::Failed:
            return "Failed";
        default:
            return "Unknown";
        }
    }

} // namespace HIKARI::TOOLS::BAKING
