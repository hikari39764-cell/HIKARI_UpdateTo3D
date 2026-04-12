#pragma once

#include <string>

namespace HIKARI {

    class AssetRegistry;

    class AssetJsonLoader {
    public:
        bool LoadModelDescriptors(const std::string& path, AssetRegistry& registry) const;
        bool LoadSkyDescriptors(const std::string& path, AssetRegistry& registry) const;
        bool LoadTextureDescriptors(const std::string& path, AssetRegistry& registry) const;
    };

} // namespace HIKARI
