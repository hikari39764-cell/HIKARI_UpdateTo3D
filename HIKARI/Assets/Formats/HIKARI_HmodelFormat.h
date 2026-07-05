#pragma once

#include <filesystem>
#include <string>

#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI {

    bool WriteHmodelFile(
        const std::filesystem::path& path,
        const ModelAsset& model,
        std::string& outMessage);

    bool ReadHmodelFile(
        const std::filesystem::path& path,
        ModelAsset& outModel,
        std::string& outMessage);

} // namespace HIKARI
