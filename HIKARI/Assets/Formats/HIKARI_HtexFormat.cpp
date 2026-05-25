#include "HIKARI_HtexFormat.h"

#include <array>
#include <fstream>
#include <limits>

namespace HIKARI {

    namespace {
        constexpr uint32_t kHtexMagic = 0x58455448u; // 'HTEX'
        constexpr uint32_t kHtexVersion = 1;

        struct HtexFileHeader {
            uint32_t magic = kHtexMagic;
            uint32_t version = kHtexVersion;
            uint32_t headerSize = sizeof(HtexFileHeader);
            uint32_t subresourceCount = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t depth = 1;
            uint32_t arraySize = 1;
            uint32_t mipLevels = 1;
            uint32_t format = 0;
            uint32_t dimension = static_cast<uint32_t>(HtexTextureDimension::Texture2D);
            uint32_t colorSpace = static_cast<uint32_t>(TextureAssetColorSpace::Auto);
            uint32_t usage = static_cast<uint32_t>(TextureUsage::Auto);
            uint32_t compression = static_cast<uint32_t>(TextureCompression::Auto);
            uint64_t subresourceTableOffset = 0;
            uint64_t dataOffset = 0;
        };

        struct HtexFileSubresource {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t depth = 1;
            uint32_t reserved = 0;
            uint64_t rowPitch = 0;
            uint64_t slicePitch = 0;
            uint64_t dataOffset = 0;
            uint64_t dataSize = 0;
        };

        bool IsSupportedDimension(uint32_t value) {
            return value == static_cast<uint32_t>(HtexTextureDimension::Texture2D) ||
                value == static_cast<uint32_t>(HtexTextureDimension::TextureCube);
        }

        bool FitsSizeT(uint64_t value) {
            return value <= static_cast<uint64_t>((std::numeric_limits<size_t>::max)());
        }
    }

    bool WriteHtexFile(
        const std::filesystem::path& path,
        const HtexTexture& texture,
        std::string& outMessage) {

        if (texture.width == 0 || texture.height == 0 ||
            texture.format == DXGI_FORMAT_UNKNOWN ||
            texture.subresources.empty()) {
            outMessage = "[HTEX] invalid texture data";
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "[HTEX] failed to create output directory: " + ec.message();
            return false;
        }

        std::vector<HtexFileSubresource> table;
        table.reserve(texture.subresources.size());

        HtexFileHeader header{};
        header.subresourceCount = static_cast<uint32_t>(texture.subresources.size());
        header.width = texture.width;
        header.height = texture.height;
        header.depth = texture.depth;
        header.arraySize = texture.arraySize;
        header.mipLevels = texture.mipLevels;
        header.format = static_cast<uint32_t>(texture.format);
        header.dimension = static_cast<uint32_t>(texture.dimension);
        header.colorSpace = static_cast<uint32_t>(texture.colorSpace);
        header.usage = static_cast<uint32_t>(texture.usage);
        header.compression = static_cast<uint32_t>(texture.compression);
        header.subresourceTableOffset = sizeof(HtexFileHeader);
        header.dataOffset = sizeof(HtexFileHeader) + sizeof(HtexFileSubresource) * texture.subresources.size();

        uint64_t cursor = header.dataOffset;
        for (const HtexSubresource& source : texture.subresources) {
            HtexFileSubresource entry{};
            entry.width = source.width;
            entry.height = source.height;
            entry.depth = source.depth;
            entry.rowPitch = source.rowPitch;
            entry.slicePitch = source.slicePitch;
            entry.dataOffset = cursor;
            entry.dataSize = static_cast<uint64_t>(source.data.size());
            cursor += entry.dataSize;
            table.push_back(entry);
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            outMessage = "[HTEX] failed to open for write: " + path.generic_string();
            return false;
        }

        ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
        ofs.write(reinterpret_cast<const char*>(table.data()), static_cast<std::streamsize>(table.size() * sizeof(HtexFileSubresource)));
        for (const HtexSubresource& source : texture.subresources) {
            if (!source.data.empty()) {
                ofs.write(
                    reinterpret_cast<const char*>(source.data.data()),
                    static_cast<std::streamsize>(source.data.size()));
            }
        }

        if (!ofs.good()) {
            outMessage = "[HTEX] failed while writing: " + path.generic_string();
            return false;
        }

        outMessage = "[HTEX] wrote " + path.generic_string();
        return true;
    }

    bool ReadHtexFile(
        const std::filesystem::path& path,
        HtexTexture& outTexture,
        std::string& outMessage) {

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HTEX] failed to open: " + path.generic_string();
            return false;
        }

        HtexFileHeader header{};
        ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!ifs.good()) {
            outMessage = "[HTEX] failed to read header: " + path.generic_string();
            return false;
        }

        if (header.magic != kHtexMagic || header.version != kHtexVersion ||
            header.headerSize != sizeof(HtexFileHeader) ||
            header.subresourceCount == 0 ||
            !IsSupportedDimension(header.dimension)) {
            outMessage = "[HTEX] invalid or unsupported file: " + path.generic_string();
            return false;
        }

        std::vector<HtexFileSubresource> table(header.subresourceCount);
        ifs.seekg(static_cast<std::streamoff>(header.subresourceTableOffset), std::ios::beg);
        ifs.read(
            reinterpret_cast<char*>(table.data()),
            static_cast<std::streamsize>(table.size() * sizeof(HtexFileSubresource)));
        if (!ifs.good()) {
            outMessage = "[HTEX] failed to read subresource table: " + path.generic_string();
            return false;
        }

        HtexTexture texture{};
        texture.version = header.version;
        texture.width = header.width;
        texture.height = header.height;
        texture.depth = header.depth;
        texture.arraySize = header.arraySize;
        texture.mipLevels = header.mipLevels;
        texture.format = static_cast<DXGI_FORMAT>(header.format);
        texture.dimension = static_cast<HtexTextureDimension>(header.dimension);
        texture.colorSpace = static_cast<TextureAssetColorSpace>(header.colorSpace);
        texture.usage = static_cast<TextureUsage>(header.usage);
        texture.compression = static_cast<TextureCompression>(header.compression);
        texture.subresources.reserve(table.size());

        for (const HtexFileSubresource& entry : table) {
            if (!FitsSizeT(entry.dataSize)) {
                outMessage = "[HTEX] subresource too large for this platform: " + path.generic_string();
                return false;
            }

            HtexSubresource subresource{};
            subresource.width = entry.width;
            subresource.height = entry.height;
            subresource.depth = entry.depth;
            subresource.rowPitch = entry.rowPitch;
            subresource.slicePitch = entry.slicePitch;
            subresource.data.resize(static_cast<size_t>(entry.dataSize));

            ifs.seekg(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
            if (!subresource.data.empty()) {
                ifs.read(
                    reinterpret_cast<char*>(subresource.data.data()),
                    static_cast<std::streamsize>(subresource.data.size()));
                if (!ifs.good()) {
                    outMessage = "[HTEX] failed to read subresource data: " + path.generic_string();
                    return false;
                }
            }

            texture.subresources.push_back(std::move(subresource));
        }

        outTexture = std::move(texture);
        outMessage = "[HTEX] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI
