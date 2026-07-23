#pragma once

#include <filesystem>
#include <string_view>

namespace HIKARI {

    // Companion sources are copied into the project because another asset
    // references them, but they never become independently selectable assets.
    bool IsAssetCompanionSource(const std::filesystem::path& path);
    bool IsSourceOnlyAssetDependencyRole(std::string_view role);

} // namespace HIKARI
