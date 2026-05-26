#pragma once

#include <cstdint>
#include <filesystem>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

    struct ProjectSettings {
        uint32_t version = 1;
        AssetGuid startupSceneGuid{};
    };

    class ProjectSettingsService {
    public:
        bool Load(const std::filesystem::path& projectRoot);
        bool Save() const;

        const ProjectSettings& GetSettings() const;
        ProjectSettings& GetSettings();

        bool SetStartupSceneGuid(const AssetGuid& guid);

    private:
        std::filesystem::path projectRoot_{};
        std::filesystem::path settingsPath_{};
        ProjectSettings settings_{};
    };

} // namespace HIKARI
