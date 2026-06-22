#include "HIKARI_HmatFormat.h"

#include <algorithm>
#include <fstream>
#include <type_traits>
#include <utility>

namespace HIKARI {

    namespace {
        constexpr uint32_t kHmatMagic = 0x54414D48u; // 'HMAT'
        constexpr uint32_t kHmatVersion = 3;
        constexpr uint32_t kMaxStringBytes = 1024u * 1024u;

        struct HmatFileHeader {
            uint32_t magic = kHmatMagic;
            uint32_t version = kHmatVersion;
            uint32_t headerSize = sizeof(HmatFileHeader);
            uint32_t reserved = 0;
        };

        template<class T>
        bool WritePod(std::ofstream& ofs, const T& value) {
            static_assert(std::is_trivially_copyable_v<T>);
            ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return ofs.good();
        }

        template<class T>
        bool ReadPod(std::ifstream& ifs, T& value) {
            static_assert(std::is_trivially_copyable_v<T>);
            ifs.read(reinterpret_cast<char*>(&value), sizeof(T));
            return ifs.good();
        }

        bool WriteBool(std::ofstream& ofs, bool value) {
            const uint8_t stored = value ? 1u : 0u;
            return WritePod(ofs, stored);
        }

        bool ReadBool(std::ifstream& ifs, bool& value) {
            uint8_t stored = 0;
            if (!ReadPod(ifs, stored)) {
                return false;
            }
            value = stored != 0;
            return true;
        }

        bool WriteString(std::ofstream& ofs, const std::string& value) {
            if (value.size() > kMaxStringBytes) {
                return false;
            }

            const uint32_t size = static_cast<uint32_t>(value.size());
            if (!WritePod(ofs, size)) {
                return false;
            }
            if (size > 0) {
                ofs.write(value.data(), static_cast<std::streamsize>(size));
            }
            return ofs.good();
        }

        bool ReadString(std::ifstream& ifs, std::string& value) {
            uint32_t size = 0;
            if (!ReadPod(ifs, size) || size > kMaxStringBytes) {
                return false;
            }

            value.resize(size);
            if (size > 0) {
                ifs.read(value.data(), static_cast<std::streamsize>(size));
            }
            return ifs.good();
        }

        bool WriteSlot(std::ofstream& ofs, const MaterialTextureSlotData& slot) {
            return WriteBool(ofs, slot.useTexture) &&
                WriteString(ofs, slot.textureAssetGuid.value) &&
                WritePod(ofs, slot.texCoord) &&
                WritePod(ofs, slot.uvScale) &&
                WritePod(ofs, slot.uvOffset) &&
                WritePod(ofs, slot.uvRotation);
        }

        bool ReadSlot(std::ifstream& ifs, MaterialTextureSlotData& slot, uint32_t version) {
            if (!ReadBool(ifs, slot.useTexture) ||
                !ReadString(ifs, slot.textureAssetGuid.value)) {
                return false;
            }
            if (version >= 3u &&
                (!ReadPod(ifs, slot.texCoord) ||
                 !ReadPod(ifs, slot.uvScale) ||
                 !ReadPod(ifs, slot.uvOffset) ||
                 !ReadPod(ifs, slot.uvRotation))) {
                return false;
            }
            slot.texCoord = std::clamp(slot.texCoord, 0, 1);
            return true;
        }
    }

    bool WriteHmatFile(
        const std::filesystem::path& path,
        const PbrMaterialAssetData& material,
        std::string& outMessage) {

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "[HMAT] failed to create output directory: " + ec.message();
            return false;
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            outMessage = "[HMAT] failed to open for write: " + path.generic_string();
            return false;
        }

        const HmatFileHeader header{};
        const bool ok =
            WritePod(ofs, header) &&
            WritePod(ofs, material.version) &&
            WriteString(ofs, material.materialName) &&
            WriteSlot(ofs, material.baseColorTexture) &&
            WriteSlot(ofs, material.normalTexture) &&
            WriteSlot(ofs, material.metallicRoughnessTexture) &&
            WriteSlot(ofs, material.occlusionTexture) &&
            WriteSlot(ofs, material.emissiveTexture) &&
            WritePod(ofs, material.baseColorFactor) &&
            WritePod(ofs, material.metallicFactor) &&
            WritePod(ofs, material.roughnessFactor) &&
            WritePod(ofs, material.normalScale) &&
            WritePod(ofs, material.occlusionStrength) &&
            WritePod(ofs, material.emissiveFactor) &&
            WritePod(ofs, material.emissiveStrength) &&
            WriteBool(ofs, material.doubleSided) &&
            WriteBool(ofs, material.unlit) &&
            WriteSlot(ofs, material.specularTexture) &&
            WriteSlot(ofs, material.specularColorTexture) &&
            WritePod(ofs, material.specularFactor) &&
            WritePod(ofs, material.specularColorFactor);

        if (!ok || !ofs.good()) {
            outMessage = "[HMAT] failed while writing: " + path.generic_string();
            return false;
        }

        outMessage = "[HMAT] wrote " + path.generic_string();
        return true;
    }

    bool ReadHmatFile(
        const std::filesystem::path& path,
        PbrMaterialAssetData& outMaterial,
        std::string& outMessage) {

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HMAT] failed to open: " + path.generic_string();
            return false;
        }

        HmatFileHeader header{};
        if (!ReadPod(ifs, header) ||
            header.magic != kHmatMagic ||
            header.version < 1u ||
            header.version > kHmatVersion ||
            header.headerSize != sizeof(HmatFileHeader)) {
            outMessage = "[HMAT] invalid or unsupported file: " + path.generic_string();
            return false;
        }

        PbrMaterialAssetData material{};
        const bool ok =
            ReadPod(ifs, material.version) &&
            ReadString(ifs, material.materialName) &&
            ReadSlot(ifs, material.baseColorTexture, header.version) &&
            ReadSlot(ifs, material.normalTexture, header.version) &&
            ReadSlot(ifs, material.metallicRoughnessTexture, header.version) &&
            ReadSlot(ifs, material.occlusionTexture, header.version) &&
            ReadSlot(ifs, material.emissiveTexture, header.version) &&
            ReadPod(ifs, material.baseColorFactor) &&
            ReadPod(ifs, material.metallicFactor) &&
            ReadPod(ifs, material.roughnessFactor) &&
            ReadPod(ifs, material.normalScale) &&
            ReadPod(ifs, material.occlusionStrength) &&
            ReadPod(ifs, material.emissiveFactor) &&
            ReadPod(ifs, material.emissiveStrength) &&
            ReadBool(ifs, material.doubleSided) &&
            ReadBool(ifs, material.unlit);

        if (ok && header.version >= 2u) {
            if (!ReadSlot(ifs, material.specularTexture, header.version) ||
                !ReadSlot(ifs, material.specularColorTexture, header.version) ||
                !ReadPod(ifs, material.specularFactor) ||
                !ReadPod(ifs, material.specularColorFactor)) {
                outMessage = "[HMAT] failed while reading v2 PBR specular fields: " + path.generic_string();
                return false;
            }
        }

        if (!ok || !ifs.good()) {
            outMessage = "[HMAT] failed while reading: " + path.generic_string();
            return false;
        }

        outMaterial = std::move(material);
        outMessage = "[HMAT] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI
