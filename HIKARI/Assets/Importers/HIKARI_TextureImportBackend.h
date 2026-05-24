#pragma once

#include <filesystem>
#include <string>

#include "HIKARI_TextureImportSettings.h"

namespace HIKARI {

    class ITextureImportBackend {
    public:
        virtual ~ITextureImportBackend() = default;

        virtual bool IsAvailable() const = 0;

        virtual bool Inspect(
            const std::filesystem::path& sourcePath,
            TextureImportSettings& inOutSettings,
            std::string& outMessage) = 0;

        virtual bool ConvertToDds(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const TextureImportSettings& settings,
            std::string& outMessage) = 0;
    };

} // namespace HIKARI
