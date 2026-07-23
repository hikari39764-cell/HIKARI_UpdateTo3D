#include "Assets/HIKARI_AssetSourcePolicy.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace HIKARI {

    bool IsAssetCompanionSource(const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        return extension == ".bin" ||
            extension == ".mtl";
    }

    bool IsSourceOnlyAssetDependencyRole(std::string_view role) {
        return role == "SourceBuffer" ||
            role == "SourceCompanion";
    }

} // namespace HIKARI
