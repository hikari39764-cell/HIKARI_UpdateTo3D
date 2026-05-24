#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "HIKARI_AssetGuid.h"
#include "HIKARI_AssetTypes.h"

namespace HIKARI {

    struct AssetDependencyDesc {
        AssetGuid guid{};
        std::string path{};
        std::string role{};
    };

    struct AssetArtifactDesc {
        std::string role{};
        std::string path{};
        std::string format{};
    };

    struct AssetMeta {
        uint32_t metaVersion = 1;

        AssetGuid guid{};
        AssetType type = AssetType::Unknown;

        std::string importerId{};
        uint32_t importerVersion = 1;

        std::string sourcePath{};
        std::string displayName{};

        std::vector<AssetDependencyDesc> dependencies{};
        std::vector<AssetArtifactDesc> artifacts{};

        std::string importSettingsJson{};
    };

} // namespace HIKARI
