#include "Assets/Collision/HIKARI_ModelCollisionSetupGeometry.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        constexpr std::array<char, 8> kMagic{
            'H', 'C', 'S', 'E', 'T', 'G', 'E', 'O'
        };
        constexpr uint32_t kVersion = 1u;
        constexpr uint32_t kMaximumGeometryShapes = 65536u;
        constexpr uint32_t kMaximumVerticesPerShape = 16u * 1024u * 1024u;
        constexpr uint32_t kMaximumIndicesPerShape = 48u * 1024u * 1024u;

        template<class TValue>
        bool WritePod(std::ostream& stream, const TValue& value) {
            static_assert(std::is_trivially_copyable_v<TValue>);
            stream.write(
                reinterpret_cast<const char*>(&value),
                static_cast<std::streamsize>(sizeof(TValue)));
            return stream.good();
        }

        template<class TValue>
        bool ReadPod(std::istream& stream, TValue& value) {
            static_assert(std::is_trivially_copyable_v<TValue>);
            stream.read(
                reinterpret_cast<char*>(&value),
                static_cast<std::streamsize>(sizeof(TValue)));
            return stream.good();
        }

        bool HasGeometry(const ModelCollisionShape& shape) noexcept {
            return !shape.vertices.empty() || !shape.indices.empty();
        }

        bool RequiresGeometry(const ModelCollisionShape& shape) noexcept {
            return shape.type == CollisionGeometryShapeType::ConvexHull ||
                shape.type == CollisionGeometryShapeType::TriangleMesh;
        }
    }

    std::filesystem::path GetModelCollisionSetupGeometryPath(
        const std::filesystem::path& setupPath) {

        return std::filesystem::path(setupPath.string() + ".geometry.bin");
    }

    bool LoadModelCollisionSetupGeometry(
        const std::filesystem::path& setupPath,
        ModelCollisionSetup& setup,
        std::string& outMessage) {

        const bool geometryRequired = std::any_of(
            setup.shapes.begin(),
            setup.shapes.end(),
            RequiresGeometry);
        const std::filesystem::path path =
            GetModelCollisionSetupGeometryPath(setupPath);
        std::error_code ec{};
        if (!std::filesystem::exists(path, ec)) {
            if (geometryRequired) {
                outMessage = "collision setup geometry sidecar is missing";
                return false;
            }
            return true;
        }

        std::ifstream stream(path, std::ios::binary);
        std::array<char, 8> magic{};
        uint32_t version = 0u;
        uint32_t shapeCount = 0u;
        stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!stream.good() || magic != kMagic ||
            !ReadPod(stream, version) || version != kVersion ||
            !ReadPod(stream, shapeCount) ||
            shapeCount > kMaximumGeometryShapes) {
            outMessage = "collision setup geometry sidecar is invalid";
            return false;
        }

        std::unordered_map<uint64_t, ModelCollisionShape*> byId{};
        for (ModelCollisionShape& shape : setup.shapes) {
            byId.emplace(shape.id, &shape);
        }
        for (uint32_t shapeIndex = 0u; shapeIndex < shapeCount; ++shapeIndex) {
            uint64_t id = 0u;
            uint32_t vertexCount = 0u;
            uint32_t indexCount = 0u;
            if (!ReadPod(stream, id) ||
                !ReadPod(stream, vertexCount) ||
                !ReadPod(stream, indexCount) ||
                vertexCount > kMaximumVerticesPerShape ||
                indexCount > kMaximumIndicesPerShape) {
                outMessage = "collision setup geometry sidecar is truncated";
                return false;
            }
            const auto found = byId.find(id);
            if (found == byId.end()) {
                outMessage = "collision setup geometry references an unknown shape";
                return false;
            }
            ModelCollisionShape& shape = *found->second;
            shape.vertices.resize(vertexCount);
            shape.indices.resize(indexCount);
            for (MATH::Vec3& vertex : shape.vertices) {
                if (!ReadPod(stream, vertex.x) ||
                    !ReadPod(stream, vertex.y) ||
                    !ReadPod(stream, vertex.z)) {
                    outMessage = "collision setup geometry vertices are truncated";
                    return false;
                }
            }
            for (uint32_t& index : shape.indices) {
                if (!ReadPod(stream, index)) {
                    outMessage = "collision setup geometry indices are truncated";
                    return false;
                }
            }
        }
        for (const ModelCollisionShape& shape : setup.shapes) {
            if (RequiresGeometry(shape) && shape.vertices.empty()) {
                outMessage = "collision setup geometry is incomplete";
                return false;
            }
        }
        return true;
    }

    bool SaveModelCollisionSetupGeometry(
        const std::filesystem::path& setupPath,
        const ModelCollisionSetup& setup,
        std::string& outMessage) {

        const std::filesystem::path path =
            GetModelCollisionSetupGeometryPath(setupPath);
        const uint32_t shapeCount = static_cast<uint32_t>(std::count_if(
            setup.shapes.begin(),
            setup.shapes.end(),
            HasGeometry));
        if (shapeCount == 0u) {
            std::error_code ec{};
            std::filesystem::remove(path, ec);
            return true;
        }

        const std::filesystem::path temporary(path.string() + ".tmp");
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            outMessage = "failed to open collision setup geometry for write";
            return false;
        }
        stream.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        bool ok = stream.good() &&
            WritePod(stream, kVersion) &&
            WritePod(stream, shapeCount);
        for (const ModelCollisionShape& shape : setup.shapes) {
            if (!HasGeometry(shape)) {
                continue;
            }
            if (shape.vertices.size() > kMaximumVerticesPerShape ||
                shape.indices.size() > kMaximumIndicesPerShape ||
                shape.vertices.size() >
                    (std::numeric_limits<uint32_t>::max)() ||
                shape.indices.size() >
                    (std::numeric_limits<uint32_t>::max)()) {
                stream.close();
                std::filesystem::remove(temporary);
                outMessage = "collision setup geometry is too large";
                return false;
            }
            const uint32_t vertexCount = static_cast<uint32_t>(
                shape.vertices.size());
            const uint32_t indexCount = static_cast<uint32_t>(
                shape.indices.size());
            ok = ok && WritePod(stream, shape.id) &&
                WritePod(stream, vertexCount) &&
                WritePod(stream, indexCount);
            for (const MATH::Vec3& vertex : shape.vertices) {
                ok = ok && WritePod(stream, vertex.x) &&
                    WritePod(stream, vertex.y) &&
                    WritePod(stream, vertex.z);
            }
            for (uint32_t index : shape.indices) {
                ok = ok && WritePod(stream, index);
            }
        }
        stream.flush();
        if (!ok || !stream.good()) {
            stream.close();
            std::filesystem::remove(temporary);
            outMessage = "failed while writing collision setup geometry";
            return false;
        }
        stream.close();

        std::error_code ec{};
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            outMessage = "failed to replace collision setup geometry: " +
                ec.message();
            return false;
        }
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
