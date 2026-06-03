#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::ASSETS::GEOMETRY {

    constexpr uint32_t kHcmeshMagic = 0x48434D48u; // HCMH
    constexpr uint32_t kHcmeshVersion = 3u;

    struct HcmeshHeader {
        uint32_t magic = kHcmeshMagic;
        uint32_t version = kHcmeshVersion;

        uint32_t surfaceCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        uint32_t materialSlotCount = 0;
        uint32_t flags = 0;
    };

    bool WriteHcmeshFile(
        const std::filesystem::path& path,
        const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset,
        std::string& outMessage);

    bool ReadHcmeshFile(
        const std::filesystem::path& path,
        RENDER3D::CLUSTER::ClusteredGeometryAsset& outAsset,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::GEOMETRY
