#pragma once

#include <json.hpp>

namespace HIKARI::EDITOR::ASSET_IMPORT_SETTINGS {

    bool DrawModelClusterCookSettings(
        nlohmann::json& settings,
        bool drawSectionHeader = false);

} // namespace HIKARI::EDITOR::ASSET_IMPORT_SETTINGS
