#include "HIKARI_ProjectSettings.h"

#include <filesystem>
#include <fstream>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"

namespace HIKARI {

    bool ProjectSettingsService::Load(const std::filesystem::path& projectRoot) {
        std::error_code ec{};
        projectRoot_ = projectRoot.empty()
            ? std::filesystem::current_path(ec)
            : projectRoot;
        projectRoot_ = std::filesystem::absolute(projectRoot_, ec).lexically_normal();
        if (ec) {
            projectRoot_ = projectRoot.lexically_normal();
        }

        settingsPath_ = projectRoot_ / "ProjectSettings" / "project_settings.json";
        std::filesystem::create_directories(settingsPath_.parent_path(), ec);
        if (ec) {
            HIKARI_LOG_WARN("[ProjectSettings] failed to create settings directory: " + ec.message());
            return false;
        }

        std::ifstream ifs(settingsPath_);
        if (!ifs.is_open()) {
            settings_ = ProjectSettings{};
            return Save();
        }

        nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            HIKARI_LOG_WARN("[ProjectSettings] project_settings.json parse failed; defaults are used.");
            settings_ = ProjectSettings{};
            return false;
        }

        settings_ = ProjectSettings{};
        settings_.version = root.value("version", 1u);
        settings_.startupSceneGuid.value = root.value("startupSceneGuid", std::string{});
        return true;
    }

    bool ProjectSettingsService::Save() const {
        if (settingsPath_.empty()) {
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(settingsPath_.parent_path(), ec);
        if (ec) {
            HIKARI_LOG_WARN("[ProjectSettings] failed to create settings directory: " + ec.message());
            return false;
        }

        // ProjectSettings は参照 GUID だけを保持し、Scene 本体は Asset 側に置く。
        const nlohmann::json root{
            { "version", settings_.version },
            { "startupSceneGuid", settings_.startupSceneGuid.value },
        };

        std::ofstream ofs(settingsPath_);
        if (!ofs.is_open()) {
            HIKARI_LOG_WARN("[ProjectSettings] failed to write: " + settingsPath_.generic_string());
            return false;
        }

        ofs << root.dump(2) << '\n';
        return true;
    }

    const ProjectSettings& ProjectSettingsService::GetSettings() const {
        return settings_;
    }

    ProjectSettings& ProjectSettingsService::GetSettings() {
        return settings_;
    }

    bool ProjectSettingsService::SetStartupSceneGuid(const AssetGuid& guid) {
        if (guid.IsValid() && !IsValidAssetGuid(guid.value)) {
            return false;
        }

        settings_.startupSceneGuid = guid;
        return true;
    }

} // namespace HIKARI
