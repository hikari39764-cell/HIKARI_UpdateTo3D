#pragma once

#include <filesystem>
#include <string>

#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"

namespace HIKARI::ASSETS::COLLISION {

    bool WriteHcollisionFile(
        const std::filesystem::path& path,
        const CollisionGeometryAsset& asset,
        std::string& outMessage);

    bool ReadHcollisionFile(
        const std::filesystem::path& path,
        CollisionGeometryAsset& outAsset,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::COLLISION
