#include "HIKARI_TextureImportBackend_DirectXTex.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <filesystem>
#include <iomanip>
#include <sstream>

#include "Core/HIKARI_Logger.h"

namespace HIKARI {

    namespace {
        std::string ToLowerCopy(std::string value) {
            for (char& c : value) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return value;
        }

        std::string ToHexHr(HRESULT hr) {
            std::ostringstream oss;
            oss << "0x" << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<unsigned long>(hr);
            return oss.str();
        }

        bool IsWicExtension(const std::string& ext) {
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                ext == ".bmp" || ext == ".tif" || ext == ".tiff";
        }

        HRESULT LoadScratchImage(
            const std::filesystem::path& sourcePath,
            DirectX::TexMetadata& metadata,
            DirectX::ScratchImage& image,
            std::string& outMessage) {

            const std::string ext = ToLowerCopy(sourcePath.extension().string());
            const std::wstring widePath = sourcePath.wstring();

            if (ext == ".dds") {
                return DirectX::LoadFromDDSFile(
                    widePath.c_str(),
                    DirectX::DDS_FLAGS_NONE,
                    &metadata,
                    image);
            }
            if (ext == ".tga") {
                return DirectX::LoadFromTGAFile(
                    widePath.c_str(),
                    DirectX::TGA_FLAGS_NONE,
                    &metadata,
                    image);
            }
            if (ext == ".hdr") {
                return DirectX::LoadFromHDRFile(
                    widePath.c_str(),
                    &metadata,
                    image);
            }
            if (IsWicExtension(ext)) {
                return DirectX::LoadFromWICFile(
                    widePath.c_str(),
                    DirectX::WIC_FLAGS_NONE,
                    &metadata,
                    image);
            }

            outMessage = "[DirectXTexBackend] unsupported texture extension: " + ext;
            return E_NOTIMPL;
        }

        bool IsHdrLike(const std::filesystem::path& sourcePath, const DirectX::TexMetadata& metadata, TextureUsage usage) {
            const std::string ext = ToLowerCopy(sourcePath.extension().string());
            if (ext == ".hdr" || ext == ".exr") {
                return true;
            }
            if (usage == TextureUsage::SkyCubemap ||
                usage == TextureUsage::IblIrradiance ||
                usage == TextureUsage::IblPrefiltered) {
                return true;
            }
            return DirectX::IsTypeless(metadata.format) == false &&
                (metadata.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                    metadata.format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
                    metadata.format == DXGI_FORMAT_R11G11B10_FLOAT ||
                    metadata.format == DXGI_FORMAT_BC6H_UF16 ||
                    metadata.format == DXGI_FORMAT_BC6H_SF16);
        }

        DXGI_FORMAT ResolveWorkingFormat(
            const std::filesystem::path& sourcePath,
            const DirectX::TexMetadata& metadata,
            const TextureImportSettings& settings) {

            if (IsHdrLike(sourcePath, metadata, settings.usage)) {
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            }

            if (settings.colorSpace == TextureAssetColorSpace::Srgb) {
                return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            }

            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        DXGI_FORMAT ResolveCompressedFormat(
            const std::filesystem::path& sourcePath,
            const DirectX::TexMetadata& metadata,
            const TextureImportSettings& settings) {

            TextureCompression compression = settings.compression;
            if (compression == TextureCompression::Auto) {
                switch (settings.usage) {
                case TextureUsage::Normal:
                    compression = TextureCompression::BC5;
                    break;
                case TextureUsage::BaseColor:
                case TextureUsage::Emissive:
                case TextureUsage::UI:
                    compression = TextureCompression::BC7;
                    break;
                case TextureUsage::MetallicRoughness:
                case TextureUsage::Occlusion:
                case TextureUsage::Mask:
                    compression = TextureCompression::BC7;
                    break;
                case TextureUsage::SkyCubemap:
                case TextureUsage::IblIrradiance:
                case TextureUsage::IblPrefiltered:
                    compression = TextureCompression::None;
                    break;
                default:
                    compression = IsHdrLike(sourcePath, metadata, settings.usage)
                        ? TextureCompression::None
                        : TextureCompression::BC7;
                    break;
                }
            }

            switch (compression) {
            case TextureCompression::BC1:
                return settings.colorSpace == TextureAssetColorSpace::Srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
            case TextureCompression::BC3:
                return settings.colorSpace == TextureAssetColorSpace::Srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
            case TextureCompression::BC4:
                return DXGI_FORMAT_BC4_UNORM;
            case TextureCompression::BC5:
                return DXGI_FORMAT_BC5_UNORM;
            case TextureCompression::BC6H:
                return DXGI_FORMAT_BC6H_UF16;
            case TextureCompression::BC7:
                return settings.colorSpace == TextureAssetColorSpace::Srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
            case TextureCompression::None:
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        DirectX::TEX_FILTER_FLAGS ResolveFilterFlags(TextureAssetColorSpace colorSpace) {
            if (colorSpace == TextureAssetColorSpace::Srgb) {
                return static_cast<DirectX::TEX_FILTER_FLAGS>(
                    DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_SRGB);
            }
            return DirectX::TEX_FILTER_DEFAULT;
        }

        DirectX::TEX_COMPRESS_FLAGS ResolveCompressFlags(TextureAssetColorSpace colorSpace) {
            DirectX::TEX_COMPRESS_FLAGS flags = DirectX::TEX_COMPRESS_DEFAULT;
            if (colorSpace == TextureAssetColorSpace::Srgb) {
                flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                    flags | DirectX::TEX_COMPRESS_SRGB_OUT);
            }
            return flags;
        }

        bool NeedsMipGeneration(const TextureImportSettings& settings, const DirectX::TexMetadata& metadata) {
            if (settings.mipPolicy == TextureMipPolicy::None ||
                settings.mipPolicy == TextureMipPolicy::Preserve) {
                return false;
            }
            if (metadata.IsCubemap()) {
                return false;
            }
            return metadata.mipLevels <= 1;
        }

        const DirectX::ScratchImage& SelectImage(const DirectX::ScratchImage& source, const DirectX::ScratchImage& candidate) {
            return candidate.GetImageCount() > 0 ? candidate : source;
        }
    }

    bool DirectXTexTextureImportBackend::IsAvailable() const {
        return true;
    }

    bool DirectXTexTextureImportBackend::Inspect(
        const std::filesystem::path& sourcePath,
        TextureImportSettings& inOutSettings,
        std::string& outMessage) {

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image{};
        const HRESULT hr = LoadScratchImage(sourcePath, metadata, image, outMessage);
        if (FAILED(hr)) {
            if (outMessage.empty()) {
                outMessage = "[DirectXTexBackend] inspect failed: " + sourcePath.generic_string() + " hr=" + ToHexHr(hr);
            }
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        inOutSettings.dimension = metadata.IsCubemap()
            ? TextureAssetDimension::TextureCube
            : TextureAssetDimension::Texture2D;

        if (inOutSettings.colorSpace == TextureAssetColorSpace::Auto) {
            inOutSettings.colorSpace = DirectX::IsSRGB(metadata.format)
                ? TextureAssetColorSpace::Srgb
                : TextureAssetColorSpace::Linear;
        }

        std::ostringstream oss;
        oss << "[DirectXTexBackend] inspected "
            << sourcePath.generic_string()
            << " width=" << metadata.width
            << " height=" << metadata.height
            << " mips=" << metadata.mipLevels
            << " array=" << metadata.arraySize
            << " cubemap=" << (metadata.IsCubemap() ? "true" : "false");
        outMessage = oss.str();
        HIKARI_LOG_INFO(outMessage);
        return true;
    }

    bool DirectXTexTextureImportBackend::ConvertToDds(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath,
        const TextureImportSettings& settings,
        std::string& outMessage) {

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage loaded{};
        HRESULT hr = LoadScratchImage(sourcePath, metadata, loaded, outMessage);
        if (FAILED(hr)) {
            if (outMessage.empty()) {
                outMessage = "[DirectXTexBackend] load failed: " + sourcePath.generic_string() + " hr=" + ToHexHr(hr);
            }
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            outMessage = "[DirectXTexBackend] failed to create output directory: " + outputPath.parent_path().generic_string();
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        DirectX::ScratchImage working{};
        const DXGI_FORMAT workingFormat = ResolveWorkingFormat(sourcePath, metadata, settings);
        if (metadata.format != workingFormat && !metadata.IsCubemap()) {
            hr = DirectX::Convert(
                loaded.GetImages(),
                loaded.GetImageCount(),
                metadata,
                workingFormat,
                ResolveFilterFlags(settings.colorSpace),
                DirectX::TEX_THRESHOLD_DEFAULT,
                working);
            if (FAILED(hr)) {
                outMessage = "[DirectXTexBackend] format conversion failed: " + sourcePath.generic_string() + " hr=" + ToHexHr(hr);
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }
        }

        const DirectX::ScratchImage& converted = SelectImage(loaded, working);
        DirectX::ScratchImage mipmapped{};
        const DirectX::TexMetadata convertedMetadata = converted.GetMetadata();
        if (NeedsMipGeneration(settings, convertedMetadata) && !DirectX::IsCompressed(convertedMetadata.format)) {
            hr = DirectX::GenerateMipMaps(
                converted.GetImages(),
                converted.GetImageCount(),
                convertedMetadata,
                ResolveFilterFlags(settings.colorSpace),
                0,
                mipmapped);
            if (FAILED(hr)) {
                outMessage = "[DirectXTexBackend] mipmap generation failed: " + sourcePath.generic_string() + " hr=" + ToHexHr(hr);
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }
        }

        const DirectX::ScratchImage& mipSource = SelectImage(converted, mipmapped);
        const DirectX::TexMetadata mipMetadata = mipSource.GetMetadata();

        DirectX::ScratchImage compressed{};
        const DXGI_FORMAT compressedFormat = ResolveCompressedFormat(sourcePath, mipMetadata, settings);
        if (compressedFormat != DXGI_FORMAT_UNKNOWN && !mipMetadata.IsCubemap()) {
            hr = DirectX::Compress(
                mipSource.GetImages(),
                mipSource.GetImageCount(),
                mipMetadata,
                compressedFormat,
                ResolveCompressFlags(settings.colorSpace),
                DirectX::TEX_THRESHOLD_DEFAULT,
                compressed);
            if (FAILED(hr)) {
                outMessage = "[DirectXTexBackend] block compression failed: " + sourcePath.generic_string() + " hr=" + ToHexHr(hr);
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }
        }

        const DirectX::ScratchImage& finalImage = SelectImage(mipSource, compressed);
        hr = DirectX::SaveToDDSFile(
            finalImage.GetImages(),
            finalImage.GetImageCount(),
            finalImage.GetMetadata(),
            DirectX::DDS_FLAGS_FORCE_DX10_EXT,
            outputPath.wstring().c_str());
        if (FAILED(hr)) {
            outMessage = "[DirectXTexBackend] SaveToDDSFile failed: " + outputPath.generic_string() + " hr=" + ToHexHr(hr);
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        outMessage = "[DirectXTexBackend] wrote DDS: " + outputPath.generic_string();
        HIKARI_LOG_INFO(outMessage);
        return true;
    }

} // namespace HIKARI
