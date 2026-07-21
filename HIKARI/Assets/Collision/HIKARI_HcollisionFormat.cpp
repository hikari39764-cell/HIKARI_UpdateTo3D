#include "Assets/Collision/HIKARI_HcollisionFormat.h"

#include <array>
#include <fstream>
#include <limits>
#include <new>
#include <type_traits>

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        constexpr std::array<char, 8> kMagic{
            'H', 'C', 'O', 'L', 'L', 'I', 'S', 'N'
        };
        constexpr uint32_t kFormatVersion = 3u;
        constexpr uint32_t kEndianMarker = 0x01020304u;
        constexpr uint32_t kMaximumGuidBytes = 4096u;
        constexpr uint32_t kMaximumShapes = 1024u * 1024u;
        constexpr uint32_t kMaximumVertices = 64u * 1024u * 1024u;
        constexpr uint32_t kMaximumIndices = 192u * 1024u * 1024u;
        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;
        constexpr uint64_t kHeaderBytes = 68u;
        // id/type/reserved (16) + transform/size (36) + radius/height (8)
        // + four geometry ranges (16).
        constexpr uint64_t kSerializedShapeBytes = 76u;
        constexpr uint64_t kSerializedVertexBytes = 12u;
        constexpr uint64_t kSerializedIndexBytes = 4u;
        constexpr uint64_t kMaximumPayloadBytes = 2ull * 1024ull *
            1024ull * 1024ull;

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

        bool WriteVec3(std::ostream& stream, const MATH::Vec3& value) {
            return WritePod(stream, value.x) &&
                WritePod(stream, value.y) &&
                WritePod(stream, value.z);
        }

        bool ReadVec3(std::istream& stream, MATH::Vec3& value) {
            return ReadPod(stream, value.x) &&
                ReadPod(stream, value.y) &&
                ReadPod(stream, value.z);
        }

        bool WriteBounds(std::ostream& stream, const Bounds& bounds) {
            return WriteVec3(stream, bounds.min) &&
                WriteVec3(stream, bounds.max);
        }

        bool ReadBounds(std::istream& stream, Bounds& bounds) {
            return ReadVec3(stream, bounds.min) &&
                ReadVec3(stream, bounds.max);
        }

        void HashBytes(
            uint64_t& hash,
            const void* bytes,
            size_t byteCount) noexcept {

            const auto* data = static_cast<const uint8_t*>(bytes);
            for (size_t index = 0u; index < byteCount; ++index) {
                hash ^= data[index];
                hash *= kFnvPrime;
            }
        }

        template<class TValue>
        void HashPod(uint64_t& hash, const TValue& value) noexcept {
            static_assert(std::is_trivially_copyable_v<TValue>);
            HashBytes(hash, &value, sizeof(value));
        }

        void HashVec3(uint64_t& hash, const MATH::Vec3& value) noexcept {
            HashPod(hash, value.x);
            HashPod(hash, value.y);
            HashPod(hash, value.z);
        }

        bool TryComputeExpectedFileSize(
            uint32_t guidBytes,
            uint32_t shapeCount,
            uint32_t vertexCount,
            uint32_t indexCount,
            uint64_t& outSize) noexcept {
            const uint64_t size = kHeaderBytes +
                static_cast<uint64_t>(guidBytes) +
                static_cast<uint64_t>(shapeCount) *
                    kSerializedShapeBytes +
                static_cast<uint64_t>(vertexCount) *
                    kSerializedVertexBytes +
                static_cast<uint64_t>(indexCount) *
                    kSerializedIndexBytes;
            if (size > kMaximumPayloadBytes) {
                return false;
            }
            outSize = size;
            return true;
        }

        bool WriteShape(
            std::ostream& stream,
            const CollisionGeometryShape& shape) {

            const uint8_t type = static_cast<uint8_t>(shape.type);
            const uint8_t reserved[7]{};
            return WritePod(stream, shape.id) &&
                WritePod(stream, type) &&
                WritePod(stream, reserved) &&
                WriteVec3(stream, shape.center) &&
                WriteVec3(stream, shape.rotationEulerDegrees) &&
                WriteVec3(stream, shape.size) &&
                WritePod(stream, shape.radius) &&
                WritePod(stream, shape.height) &&
                WritePod(stream, shape.vertexOffset) &&
                WritePod(stream, shape.vertexCount) &&
                WritePod(stream, shape.indexOffset) &&
                WritePod(stream, shape.indexCount);
        }

        bool ReadShape(
            std::istream& stream,
            CollisionGeometryShape& shape) {

            uint8_t type = 0u;
            uint8_t reserved[7]{};
            if (!ReadPod(stream, shape.id) ||
                !ReadPod(stream, type) ||
                !ReadPod(stream, reserved) ||
                !ReadVec3(stream, shape.center) ||
                !ReadVec3(stream, shape.rotationEulerDegrees) ||
                !ReadVec3(stream, shape.size) ||
                !ReadPod(stream, shape.radius) ||
                !ReadPod(stream, shape.height) ||
                !ReadPod(stream, shape.vertexOffset) ||
                !ReadPod(stream, shape.vertexCount) ||
                !ReadPod(stream, shape.indexOffset) ||
                !ReadPod(stream, shape.indexCount) ||
                type > static_cast<uint8_t>(
                    CollisionGeometryShapeType::TriangleMesh)) {
                return false;
            }
            shape.type = static_cast<CollisionGeometryShapeType>(type);
            return true;
        }
    }

    uint64_t ComputeCollisionGeometryContentHash(
        const CollisionGeometryAsset& asset) noexcept {
        uint64_t hash = kFnvOffset;
        HashBytes(
            hash,
            asset.sourceAssetGuid.data(),
            asset.sourceAssetGuid.size());
        HashVec3(hash, asset.localBounds.min);
        HashVec3(hash, asset.localBounds.max);
        for (const CollisionGeometryShape& shape : asset.shapes) {
            HashPod(hash, shape.id);
            const uint8_t type = static_cast<uint8_t>(shape.type);
            HashPod(hash, type);
            HashVec3(hash, shape.center);
            HashVec3(hash, shape.rotationEulerDegrees);
            HashVec3(hash, shape.size);
            HashPod(hash, shape.radius);
            HashPod(hash, shape.height);
            HashPod(hash, shape.vertexOffset);
            HashPod(hash, shape.vertexCount);
            HashPod(hash, shape.indexOffset);
            HashPod(hash, shape.indexCount);
        }
        for (const MATH::Vec3& vertex : asset.vertices) {
            HashVec3(hash, vertex);
        }
        for (uint32_t index : asset.indices) {
            HashPod(hash, index);
        }
        return hash;
    }

    bool WriteHcollisionFile(
        const std::filesystem::path& path,
        const CollisionGeometryAsset& asset,
        std::string& outMessage) {

        const CollisionGeometryValidationResult validation =
            ValidateCollisionGeometryAsset(asset);
        if (!validation.valid) {
            outMessage = validation.messages.empty()
                ? "[HCOLLISION] asset validation failed"
                : "[HCOLLISION] " + validation.messages.front();
            return false;
        }
        if (asset.sourceAssetGuid.size() > kMaximumGuidBytes ||
            asset.shapes.size() > kMaximumShapes ||
            asset.vertices.size() > kMaximumVertices ||
            asset.indices.size() > kMaximumIndices ||
            asset.shapes.size() >
                (std::numeric_limits<uint32_t>::max)() ||
            asset.vertices.size() >
                (std::numeric_limits<uint32_t>::max)() ||
            asset.indices.size() >
                (std::numeric_limits<uint32_t>::max)()) {
            outMessage = "[HCOLLISION] asset is too large to serialize";
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage =
                "[HCOLLISION] failed to create output directory: " +
                ec.message();
            return false;
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            outMessage = "[HCOLLISION] failed to open output: " +
                path.generic_string();
            return false;
        }

        const uint32_t guidBytes = static_cast<uint32_t>(
            asset.sourceAssetGuid.size());
        const uint32_t shapeCount = static_cast<uint32_t>(
            asset.shapes.size());
        const uint32_t vertexCount = static_cast<uint32_t>(
            asset.vertices.size());
        const uint32_t indexCount = static_cast<uint32_t>(
            asset.indices.size());
        const uint64_t contentHash =
            ComputeCollisionGeometryContentHash(asset);

        stream.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        bool ok = stream.good() &&
            WritePod(stream, kFormatVersion) &&
            WritePod(stream, kEndianMarker) &&
            WritePod(stream, asset.version) &&
            WritePod(stream, guidBytes) &&
            WritePod(stream, shapeCount) &&
            WritePod(stream, vertexCount) &&
            WritePod(stream, indexCount) &&
            WritePod(stream, contentHash) &&
            WriteBounds(stream, asset.localBounds);
        if (guidBytes > 0u) {
            stream.write(
                asset.sourceAssetGuid.data(),
                static_cast<std::streamsize>(guidBytes));
            ok = ok && stream.good();
        }
        for (const CollisionGeometryShape& shape : asset.shapes) {
            ok = ok && WriteShape(stream, shape);
        }
        for (const MATH::Vec3& vertex : asset.vertices) {
            ok = ok && WriteVec3(stream, vertex);
        }
        for (uint32_t index : asset.indices) {
            ok = ok && WritePod(stream, index);
        }
        stream.flush();
        ok = ok && stream.good();
        if (!ok) {
            outMessage = "[HCOLLISION] failed while writing: " +
                path.generic_string();
            return false;
        }
        outMessage = "[HCOLLISION] wrote " + path.generic_string();
        return true;
    }

    bool ReadHcollisionFile(
        const std::filesystem::path& path,
        CollisionGeometryAsset& outAsset,
        std::string& outMessage,
        HcollisionReadInfo* outInfo) {

        outAsset = {};
        if (outInfo != nullptr) {
            *outInfo = {};
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            outMessage = "[HCOLLISION] failed to open: " +
                path.generic_string();
            return false;
        }

        stream.seekg(0, std::ios::end);
        const std::streamoff fileEnd = stream.tellg();
        if (fileEnd < 0) {
            outMessage = "[HCOLLISION] failed to inspect file size: " +
                path.generic_string();
            return false;
        }
        const uint64_t fileSize = static_cast<uint64_t>(fileEnd);
        stream.seekg(0, std::ios::beg);

        std::array<char, 8> magic{};
        stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        uint32_t formatVersion = 0u;
        uint32_t endianMarker = 0u;
        uint32_t guidBytes = 0u;
        uint32_t shapeCount = 0u;
        uint32_t vertexCount = 0u;
        uint32_t indexCount = 0u;
        uint64_t storedHash = 0u;
        bool ok = stream.good() && magic == kMagic &&
            ReadPod(stream, formatVersion) &&
            ReadPod(stream, endianMarker) &&
            ReadPod(stream, outAsset.version) &&
            ReadPod(stream, guidBytes) &&
            ReadPod(stream, shapeCount) &&
            ReadPod(stream, vertexCount) &&
            ReadPod(stream, indexCount) &&
            ReadPod(stream, storedHash) &&
            ReadBounds(stream, outAsset.localBounds);
        if (!ok ||
            formatVersion != kFormatVersion ||
            endianMarker != kEndianMarker ||
            outAsset.version != kCollisionGeometryAssetVersion ||
            guidBytes > kMaximumGuidBytes ||
            shapeCount > kMaximumShapes ||
            vertexCount > kMaximumVertices ||
            indexCount > kMaximumIndices) {
            outMessage =
                "[HCOLLISION] invalid or unsupported v3 file; recook the model collision asset: " +
                path.generic_string();
            outAsset = {};
            return false;
        }

        uint64_t expectedFileSize = 0u;
        if (!TryComputeExpectedFileSize(
                guidBytes,
                shapeCount,
                vertexCount,
                indexCount,
                expectedFileSize) ||
            fileSize != expectedFileSize) {
            outMessage =
                "[HCOLLISION] file size does not match its header: " +
                path.generic_string();
            outAsset = {};
            return false;
        }

        try {
            outAsset.sourceAssetGuid.resize(guidBytes);
            outAsset.shapes.resize(shapeCount);
            outAsset.vertices.resize(vertexCount);
            outAsset.indices.resize(indexCount);
        } catch (const std::bad_alloc&) {
            outMessage =
                "[HCOLLISION] payload allocation failed: " +
                path.generic_string();
            outAsset = {};
            return false;
        }
        if (guidBytes > 0u) {
            stream.read(
                outAsset.sourceAssetGuid.data(),
                static_cast<std::streamsize>(guidBytes));
            ok = stream.good();
        }
        for (CollisionGeometryShape& shape : outAsset.shapes) {
            ok = ok && ReadShape(stream, shape);
        }
        for (MATH::Vec3& vertex : outAsset.vertices) {
            ok = ok && ReadVec3(stream, vertex);
        }
        for (uint32_t& index : outAsset.indices) {
            ok = ok && ReadPod(stream, index);
        }
        if (!ok ||
            ComputeCollisionGeometryContentHash(outAsset) != storedHash) {
            outMessage =
                "[HCOLLISION] payload is truncated or corrupted: " +
                path.generic_string();
            outAsset = {};
            return false;
        }

        const CollisionGeometryValidationResult validation =
            ValidateCollisionGeometryAsset(outAsset);
        if (!validation.valid) {
            outMessage = validation.messages.empty()
                ? "[HCOLLISION] validation failed"
                : "[HCOLLISION] " + validation.messages.front();
            outAsset = {};
            return false;
        }
        if (outInfo != nullptr) {
            outInfo->contentHash = storedHash;
            outInfo->fileSize = fileSize;
        }
        outMessage = "[HCOLLISION] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
