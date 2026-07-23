#include "Assets/HIKARI_AssetSourcePolicy.h"

#include <algorithm>
#include <cctype>

#include "Core/Text/HIKARI_AsciiCase.h"
#include <string>

namespace HIKARI {

    bool IsAssetCompanionSource(const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        TEXT::ToLowerAsciiInPlace(extension);
        return extension == ".bin" ||
            extension == ".mtl";
    }

    bool IsSourceOnlyAssetDependencyRole(std::string_view role) {
        return role == "SourceBuffer" ||
            role == "SourceCompanion";
    }

} // namespace HIKARI
