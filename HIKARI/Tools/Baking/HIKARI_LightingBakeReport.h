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
        ValidateOnly,
    };

    struct LightingBakeReport {
        bool success = true;
        bool manifestWritten = false;
        bool bakeFolderCreated = false;
        bool bakeFolderCleared = false;
        LightingBakeAction action = LightingBakeAction::ValidateOnly;
        LightingBakeTarget target = LightingBakeTarget::All;
        std::filesystem::path bakeRoot{};
        std::filesystem::path manifestPath{};
        uint32_t reflectionProbeRecordCount = 0;
        uint32_t lightProbeRecordCount = 0;
        uint32_t lightmapRecordCount = 0;
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
        case LightingBakeAction::ValidateOnly:
            return "ValidateOnly";
        default:
            return "Unknown";
        }
    }

} // namespace HIKARI::TOOLS::BAKING
