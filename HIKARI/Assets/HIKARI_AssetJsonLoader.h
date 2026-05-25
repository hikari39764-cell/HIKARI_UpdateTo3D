#pragma once

#include <string>

namespace HIKARI {

    class AssetRegistry;

    // Deprecated: kept only as a legacy descriptor reader. Normal asset registration
    // is built from AssetDatabase and .hikari.meta records.
    class AssetJsonLoader {
    public:
        bool LoadModelDescriptors(const std::string& path, AssetRegistry& registry) const;
        bool LoadSkyDescriptors(const std::string& path, AssetRegistry& registry) const;
        bool LoadTextureDescriptors(const std::string& path, AssetRegistry& registry) const;
        bool LoadVfxDescriptors(const std::string& path, AssetRegistry& registry) const;
    };

} // namespace HIKARI
