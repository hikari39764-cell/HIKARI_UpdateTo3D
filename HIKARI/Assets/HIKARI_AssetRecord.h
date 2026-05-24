#pragma once

#include <filesystem>
#include <string>

#include "HIKARI_AssetMeta.h"

namespace HIKARI {

    struct AssetRecord {
        AssetGuid guid{};
        AssetType type = AssetType::Unknown;

        std::filesystem::path sourcePath{};
        std::filesystem::path metaPath{};
        std::filesystem::path importedDirectory{};

        std::string displayName{};

        bool sourceExists = false;
        bool metaExists = false;
        bool importOutdated = false;
        bool lastImportSucceeded = false;

        bool duplicateGuid = false;
        bool importerMissing = false;
        bool artifactMissing = false;
        std::string lastImportMessage{};

        AssetMeta meta{};
    };

} // namespace HIKARI
