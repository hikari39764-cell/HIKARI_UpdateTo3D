#include "Assets/Geometry/HIKARI_HcmeshFormat.h"

#include <fstream>
#include <limits>
#include <type_traits>

namespace HIKARI::ASSETS::GEOMETRY {

    namespace {
        constexpr uint32_t kMaxStringBytes = 16u * 1024u * 1024u;
        constexpr uint32_t kMaxVectorCount = 64u * 1024u * 1024u;

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

        bool WriteString(std::ofstream& ofs, const std::string& value) {
            if (value.size() > kMaxStringBytes) {
                return false;
            }
            const uint32_t size = static_cast<uint32_t>(value.size());
            if (!WritePod(ofs, size)) {
                return false;
            }
            if (size > 0u) {
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
            if (size > 0u) {
                ifs.read(value.data(), static_cast<std::streamsize>(size));
            }
            return ifs.good();
        }

        template<class T>
        bool WritePodVector(std::ofstream& ofs, const std::vector<T>& values) {
            static_assert(std::is_trivially_copyable_v<T>);
            if (values.size() > kMaxVectorCount) {
                return false;
            }
            const uint32_t count = static_cast<uint32_t>(values.size());
            if (!WritePod(ofs, count)) {
                return false;
            }
            if (!values.empty()) {
                ofs.write(
                    reinterpret_cast<const char*>(values.data()),
                    static_cast<std::streamsize>(values.size() * sizeof(T)));
            }
            return ofs.good();
        }

        template<class T>
        bool ReadPodVector(std::ifstream& ifs, std::vector<T>& values) {
            static_assert(std::is_trivially_copyable_v<T>);
            uint32_t count = 0;
            if (!ReadPod(ifs, count) || count > kMaxVectorCount) {
                return false;
            }
            values.resize(count);
            if (!values.empty()) {
                ifs.read(
                    reinterpret_cast<char*>(values.data()),
                    static_cast<std::streamsize>(values.size() * sizeof(T)));
            }
            return ifs.good();
        }

        bool CountsMatchHeader(
            const HcmeshHeader& header,
            const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset) {

            return header.surfaceCount == asset.surfaces.size() &&
                header.surfaceLodRangeCount == asset.surfaceLodRanges.size() &&
                header.surfaceSectionCount == asset.surfaceSections.size() &&
                header.clusterCount == asset.clusters.size() &&
                header.pageCount == asset.pages.size() &&
                header.vertexCount == asset.packedVertices.size() &&
                header.indexCount == asset.packedIndices.size() &&
                header.meshletPrimitiveCount == asset.meshletPrimitives.size() &&
                header.materialSlotCount == asset.materialSlotMapping.size();
        }
    }

    bool WriteHcmeshFile(
        const std::filesystem::path& path,
        const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset,
        std::string& outMessage) {

        if (!asset.valid || asset.surfaces.empty() || asset.clusters.empty() ||
            asset.packedVertices.empty() || asset.packedIndices.empty() ||
            asset.meshletPrimitives.empty()) {
            outMessage = "[HCMESH] invalid clustered geometry data";
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "[HCMESH] failed to create output directory: " + ec.message();
            return false;
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            outMessage = "[HCMESH] failed to open for write: " + path.generic_string();
            return false;
        }

        HcmeshHeader header{};
        header.surfaceCount = static_cast<uint32_t>(asset.surfaces.size());
        header.surfaceLodRangeCount = static_cast<uint32_t>(asset.surfaceLodRanges.size());
        header.surfaceSectionCount = static_cast<uint32_t>(asset.surfaceSections.size());
        header.clusterCount = static_cast<uint32_t>(asset.clusters.size());
        header.pageCount = static_cast<uint32_t>(asset.pages.size());
        header.vertexCount = static_cast<uint32_t>(asset.packedVertices.size());
        header.indexCount = static_cast<uint32_t>(asset.packedIndices.size());
        header.meshletPrimitiveCount = static_cast<uint32_t>(asset.meshletPrimitives.size());
        header.materialSlotCount = static_cast<uint32_t>(asset.materialSlotMapping.size());
        header.flags = asset.flags;

        // section 順序は version で固定する。
        const bool ok =
            WritePod(ofs, header) &&
            WriteString(ofs, asset.sourceModelGuid.value) &&
            WriteString(ofs, asset.sourceModelPath) &&
            WritePod(ofs, asset.localBounds) &&
            WritePod(ofs, asset.totalTriangleCount) &&
            WritePod(ofs, asset.totalVertexCount) &&
            WritePod(ofs, asset.skippedPrimitiveCount) &&
            WritePod(ofs, asset.skippedSkinnedPrimitiveCount) &&
            WritePod(ofs, asset.skippedMorphPrimitiveCount) &&
            WritePod(ofs, asset.skippedInvalidPrimitiveCount) &&
            WritePod(ofs, asset.unsupportedPrimitiveModeCount) &&
            WritePod(ofs, asset.unsupportedFeatureCount) &&
            WritePodVector(ofs, asset.surfaces) &&
            WritePodVector(ofs, asset.surfaceLodRanges) &&
            WritePodVector(ofs, asset.surfaceSections) &&
            WritePodVector(ofs, asset.clusters) &&
            WritePodVector(ofs, asset.pages) &&
            WritePodVector(ofs, asset.packedVertices) &&
            WritePodVector(ofs, asset.packedIndices) &&
            WritePodVector(ofs, asset.meshletPrimitives) &&
            WritePodVector(ofs, asset.materialSlotMapping);

        if (!ok || !ofs.good()) {
            outMessage = "[HCMESH] failed while writing: " + path.generic_string();
            return false;
        }

        outMessage = "[HCMESH] wrote " + path.generic_string();
        return true;
    }

    bool ReadHcmeshFile(
        const std::filesystem::path& path,
        RENDER3D::CLUSTER::ClusteredGeometryAsset& outAsset,
        std::string& outMessage) {

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HCMESH] failed to open: " + path.generic_string();
            return false;
        }

        HcmeshHeader header{};
        if (!ReadPod(ifs, header) ||
            header.magic != kHcmeshMagic ||
            header.version != kHcmeshVersion) {
            outMessage = "[HCMESH] invalid or unsupported file: " + path.generic_string();
            return false;
        }

        RENDER3D::CLUSTER::ClusteredGeometryAsset asset{};
        asset.flags = header.flags;
        if (!ReadString(ifs, asset.sourceModelGuid.value) ||
            !ReadString(ifs, asset.sourceModelPath) ||
            !ReadPod(ifs, asset.localBounds) ||
            !ReadPod(ifs, asset.totalTriangleCount) ||
            !ReadPod(ifs, asset.totalVertexCount) ||
            !ReadPod(ifs, asset.skippedPrimitiveCount) ||
            !ReadPod(ifs, asset.skippedSkinnedPrimitiveCount) ||
            !ReadPod(ifs, asset.skippedMorphPrimitiveCount) ||
            !ReadPod(ifs, asset.skippedInvalidPrimitiveCount) ||
            !ReadPod(ifs, asset.unsupportedPrimitiveModeCount) ||
            !ReadPod(ifs, asset.unsupportedFeatureCount) ||
            !ReadPodVector(ifs, asset.surfaces) ||
            !ReadPodVector(ifs, asset.surfaceLodRanges) ||
            !ReadPodVector(ifs, asset.surfaceSections) ||
            !ReadPodVector(ifs, asset.clusters) ||
            !ReadPodVector(ifs, asset.pages) ||
            !ReadPodVector(ifs, asset.packedVertices) ||
            !ReadPodVector(ifs, asset.packedIndices) ||
            !ReadPodVector(ifs, asset.meshletPrimitives) ||
            !ReadPodVector(ifs, asset.materialSlotMapping)) {
            outMessage = "[HCMESH] failed while reading: " + path.generic_string();
            return false;
        }

        if (!CountsMatchHeader(header, asset)) {
            outMessage = "[HCMESH] section count mismatch: " + path.generic_string();
            return false;
        }

        asset.valid = true;
        outAsset = std::move(asset);
        outMessage = "[HCMESH] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI::ASSETS::GEOMETRY
