#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"

#include <fstream>
#include <system_error>

#include <json.hpp>

#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"

namespace HIKARI::RENDER3D {

    std::filesystem::path RenderQualityProfilePath(
        const std::filesystem::path& projectRoot) {
        return projectRoot / "ProjectSettings" / "render_quality.json";
    }

    bool LoadRenderQualityProfile(
        const std::filesystem::path& projectRoot,
        RenderQualitySettings& outSettings,
        std::string* errorMessage) {
        const std::filesystem::path path = RenderQualityProfilePath(projectRoot);
        std::ifstream input(path);
        if (!input) {
            if (errorMessage != nullptr) {
                errorMessage->clear();
            }
            return false;
        }

        const nlohmann::json root = nlohmann::json::parse(input, nullptr, false);
        if (root.is_discarded() || !root.is_object() ||
            !root.contains("renderQuality")) {
            if (errorMessage != nullptr) {
                *errorMessage = "Could not parse render quality profile: " + path.string();
            }
            return false;
        }

        std::string decodeError{};
        if (!DeserializeRenderQualitySettings(
                root["renderQuality"],
                outSettings,
                &decodeError)) {
            if (errorMessage != nullptr) {
                *errorMessage = decodeError + " Path: " + path.string();
            }
            return false;
        }
        return true;
    }

    bool SaveRenderQualityProfile(
        const std::filesystem::path& projectRoot,
        const RenderQualitySettings& settings,
        std::string* errorMessage) {
        const std::filesystem::path path = RenderQualityProfilePath(projectRoot);
        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "Could not create render quality profile directory: " +
                    ec.message();
            }
            return false;
        }

        std::ofstream output(path);
        if (!output) {
            if (errorMessage != nullptr) {
                *errorMessage = "Could not write render quality profile: " + path.string();
            }
            return false;
        }

        const nlohmann::json root{
            { "version", 1u },
            { "renderQuality", SerializeRenderQualitySettings(settings) },
        };
        output << root.dump(2) << '\n';
        if (!output.good()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Could not finish render quality profile: " + path.string();
            }
            return false;
        }
        return true;
    }

} // namespace HIKARI::RENDER3D
