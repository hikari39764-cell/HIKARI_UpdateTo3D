#include "Render3D/Cluster/HIKARI_ClusterGeometryPacked.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

#include <DirectXPackedVector.h>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        static_assert(
            kHcmeshMaxVerticesPerMeshlet <= 256u,
            "Packed meshlet primitive indices require 8-bit local vertex indices.");

        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        uint32_t AlignUp(uint32_t value, uint32_t alignment) {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        uint16_t PackSnorm16(float value) {
            const float clamped = std::clamp(value, -1.0f, 1.0f);
            const int32_t quantized =
                static_cast<int32_t>(std::round(clamped * 32767.0f));
            return static_cast<uint16_t>(
                static_cast<int16_t>(std::clamp(quantized, -32767, 32767)));
        }

        uint16_t PackHalf16(float value) {
            return static_cast<uint16_t>(DirectX::PackedVector::XMConvertFloatToHalf(value));
        }

        uint32_t PackPair16(uint16_t low, uint16_t high) {
            return static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16u);
        }

        MATH::Vec4 BoundsMin4(const Bounds& bounds) {
            return { bounds.min.x, bounds.min.y, bounds.min.z, 1.0f };
        }

        MATH::Vec4 BoundsMax4(const Bounds& bounds) {
            return { bounds.max.x, bounds.max.y, bounds.max.z, 1.0f };
        }

        MATH::Vec4 BoundsCenterRadius4(const Bounds& bounds) {
            const MATH::Vec3 center{
                (bounds.min.x + bounds.max.x) * 0.5f,
                (bounds.min.y + bounds.max.y) * 0.5f,
                (bounds.min.z + bounds.max.z) * 0.5f,
            };
            const MATH::Vec3 extent{
                bounds.max.x - center.x,
                bounds.max.y - center.y,
                bounds.max.z - center.z,
            };
            const float radius = std::sqrt(
                extent.x * extent.x +
                extent.y * extent.y +
                extent.z * extent.z);
            return { center.x, center.y, center.z, radius };
        }

        const Bounds& ResolveLodMetricBounds(const ClusterSurfaceSection& section) {
            return BOUNDS::IsUsable(section.lodMetricBounds)
                ? section.lodMetricBounds
                : section.localBounds;
        }

        template<class T>
        void AppendPod(std::vector<uint8_t>& bytes, const T& value) {
            static_assert(std::is_trivially_copyable_v<T>);
            const size_t oldSize = bytes.size();
            bytes.resize(oldSize + sizeof(T));
            std::memcpy(bytes.data() + oldSize, &value, sizeof(T));
        }

        uint32_t AlignSection(std::vector<uint8_t>& bytes) {
            const uint32_t offset = ClampToUint32(bytes.size());
            const uint32_t alignedOffset = AlignUp(offset, kClusterGeometryGpuSectionAlignment);
            bytes.resize(alignedOffset);
            return alignedOffset;
        }

        ClusterGeometrySurfaceRange ToSurfaceRange(
            uint32_t surfaceIndex,
            const ClusterSurface& source) {

            ClusterGeometrySurfaceRange range{};
            range.surfaceIndex = surfaceIndex;
            range.nodeIndex = source.nodeIndex;
            range.meshIndex = source.meshIndex;
            range.primitiveIndex = source.primitiveIndex;
            range.materialIndex = source.materialIndex;
            range.firstCluster = source.firstCluster;
            range.clusterCount = source.clusterCount;
            range.flags = source.flags;
            range.firstIndex = source.firstIndex;
            range.indexCount = source.indexCount;
            range.firstVertex = source.firstVertex;
            range.vertexCount = source.vertexCount;
            range.firstPage = source.firstPage;
            range.pageCount = source.pageCount;
            range.firstPrimitive = source.firstPrimitive;
            range.primitiveCount = source.primitiveCount;
            range.firstLodRange = source.firstLodRange;
            range.lodRangeCount = source.lodRangeCount;
            return range;
        }

        ClusterGeometryGpuSurface ToGpuSurface(const ClusterSurface& source) {
            ClusterGeometryGpuSurface gpu{};
            gpu.nodeIndex = source.nodeIndex;
            gpu.meshIndex = source.meshIndex;
            gpu.primitiveIndex = source.primitiveIndex;
            gpu.materialIndex = source.materialIndex;
            gpu.firstCluster = source.firstCluster;
            gpu.clusterCount = source.clusterCount;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.flags = source.flags;
            gpu.firstPage = source.firstPage;
            gpu.pageCount = source.pageCount;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.primitiveCount = source.primitiveCount;
            gpu.firstLodRange = source.firstLodRange;
            gpu.lodRangeCount = source.lodRangeCount;
            gpu.firstSection = source.firstSection;
            gpu.sectionCount = source.sectionCount;
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            return gpu;
        }

        ClusterGeometryGpuSurfaceLodRange ToGpuSurfaceLodRange(
            const ClusterSurfaceLodRange& source) {

            ClusterGeometryGpuSurfaceLodRange gpu{};
            gpu.surfaceIndex = source.surfaceIndex;
            gpu.lodIndex = source.lodIndex;
            gpu.firstCluster = source.firstCluster;
            gpu.clusterCount = source.clusterCount;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.firstPage = source.firstPage;
            gpu.pageCount = source.pageCount;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.primitiveCount = source.primitiveCount;
            gpu.geometricError = source.geometricError;
            gpu.minScreenRadius = source.minScreenRadius;
            gpu.flags = source.flags;
            gpu.sectionIndex = source.sectionIndex;
            return gpu;
        }

        ClusterGeometrySurfaceLodRange ToSurfaceLodRange(
            const ClusterSurfaceLodRange& source) {

            ClusterGeometrySurfaceLodRange range{};
            range.surfaceIndex = source.surfaceIndex;
            range.lodIndex = source.lodIndex;
            range.firstCluster = source.firstCluster;
            range.clusterCount = source.clusterCount;
            range.firstIndex = source.firstIndex;
            range.indexCount = source.indexCount;
            range.firstVertex = source.firstVertex;
            range.vertexCount = source.vertexCount;
            range.firstPage = source.firstPage;
            range.pageCount = source.pageCount;
            range.firstPrimitive = source.firstPrimitive;
            range.primitiveCount = source.primitiveCount;
            range.geometricError = source.geometricError;
            range.minScreenRadius = source.minScreenRadius;
            range.flags = source.flags;
            range.sectionIndex = source.sectionIndex;
            return range;
        }

        ClusterGeometryGpuSurfaceSection ToGpuSurfaceSection(
            const ClusterSurfaceSection& source) {

            ClusterGeometryGpuSurfaceSection gpu{};
            gpu.surfaceIndex = source.surfaceIndex;
            gpu.sectionIndex = source.sectionIndex;
            gpu.firstCluster = source.firstCluster;
            gpu.clusterCount = source.clusterCount;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.firstPage = source.firstPage;
            gpu.pageCount = source.pageCount;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.primitiveCount = source.primitiveCount;
            gpu.firstLodRange = source.firstLodRange;
            gpu.lodRangeCount = source.lodRangeCount;
            gpu.flags = source.flags;
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            gpu.lodMetricCenterRadius = BoundsCenterRadius4(ResolveLodMetricBounds(source));
            gpu.lodErrorBudgetNdc = source.lodErrorBudgetNdc;
            return gpu;
        }

        ClusterGeometrySurfaceSection ToSurfaceSection(
            const ClusterSurfaceSection& source) {

            ClusterGeometrySurfaceSection section{};
            section.surfaceIndex = source.surfaceIndex;
            section.sectionIndex = source.sectionIndex;
            section.firstCluster = source.firstCluster;
            section.clusterCount = source.clusterCount;
            section.firstIndex = source.firstIndex;
            section.indexCount = source.indexCount;
            section.firstVertex = source.firstVertex;
            section.vertexCount = source.vertexCount;
            section.firstPage = source.firstPage;
            section.pageCount = source.pageCount;
            section.firstPrimitive = source.firstPrimitive;
            section.primitiveCount = source.primitiveCount;
            section.firstLodRange = source.firstLodRange;
            section.lodRangeCount = source.lodRangeCount;
            section.flags = source.flags;
            section.boundsMin = BoundsMin4(source.localBounds);
            section.boundsMax = BoundsMax4(source.localBounds);
            section.lodMetricCenterRadius = BoundsCenterRadius4(ResolveLodMetricBounds(source));
            section.lodErrorBudgetNdc = source.lodErrorBudgetNdc;
            return section;
        }

        ClusterGeometryGpuCluster ToGpuCluster(const MeshCluster& source) {
            ClusterGeometryGpuCluster gpu{};
            gpu.surfaceIndex = source.surfaceIndex;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.triangleCount = source.triangleCount;
            gpu.flags = source.flags;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            gpu.sphereCenterRadius = {
                source.sphereCenter.x,
                source.sphereCenter.y,
                source.sphereCenter.z,
                source.sphereRadius
            };
            gpu.coneApex = {
                source.coneApex.x,
                source.coneApex.y,
                source.coneApex.z,
                source.coneReserved
            };
            gpu.coneAxisCutoff = {
                source.coneAxis.x,
                source.coneAxis.y,
                source.coneAxis.z,
                source.coneCutoff
            };
            return gpu;
        }

        ClusterGeometryGpuPage ToGpuPage(const ClusterPage& source) {
            ClusterGeometryGpuPage gpu{};
            gpu.firstCluster = source.firstCluster;
            gpu.clusterCount = source.clusterCount;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.primitiveCount = source.primitiveCount;
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            return gpu;
        }

        ClusterGeometryGpuMeshletPrimitive ToGpuMeshletPrimitive(
            const MeshletPrimitive& source) {

            ClusterGeometryGpuMeshletPrimitive gpu{};
            gpu.packedIndices =
                (source.i0 & 0xffu) |
                ((source.i1 & 0xffu) << 8u) |
                ((source.i2 & 0xffu) << 16u) |
                ((source.reserved0 & 0xffu) << 24u);
            return gpu;
        }

        ClusterGeometryGpuVertexPosition ToGpuVertexPosition(const ClusterVertex& source) {
            ClusterGeometryGpuVertexPosition gpu{};
            gpu.position = { source.position.x, source.position.y, source.position.z };
            return gpu;
        }

        ClusterGeometryGpuVertexAttributes ToGpuVertexAttributes(const ClusterVertex& source) {
            ClusterGeometryGpuVertexAttributes gpu{};
            gpu.normalXY =
                PackPair16(PackSnorm16(source.normal.x), PackSnorm16(source.normal.y));
            gpu.normalZ_TangentW =
                PackPair16(PackSnorm16(source.normal.z), PackSnorm16(source.tangent.w));
            gpu.tangentXY =
                PackPair16(PackSnorm16(source.tangent.x), PackSnorm16(source.tangent.y));
            gpu.tangentZ_Uv0X =
                PackPair16(PackSnorm16(source.tangent.z), PackHalf16(source.uv0.x));
            gpu.uv0Y_Uv1X =
                PackPair16(PackHalf16(source.uv0.y), PackHalf16(source.uv1.x));
            gpu.uv1Y_Reserved0 =
                PackPair16(PackHalf16(source.uv1.y), 0u);
            return gpu;
        }
    } // namespace

    bool PackClusterGeometryForGpu(
        const ClusteredGeometryAsset& asset,
        const ClusterGeometryPackOptions& options,
        ClusterGeometryPackedBytes& outPacked,
        std::string* outMessage) {

        outPacked = {};
        if (!asset.valid ||
            asset.surfaces.empty() ||
            asset.clusters.empty() ||
            asset.packedVertices.empty() ||
            asset.meshletPrimitives.empty()) {
            if (outMessage != nullptr) {
                *outMessage = "[ClusterGeometryPack] invalid source asset";
            }
            return false;
        }
        if (options.includeFallbackIndices && asset.packedIndices.empty()) {
            if (outMessage != nullptr) {
                *outMessage = "[ClusterGeometryPack] fallback index data is required but missing";
            }
            return false;
        }

        ClusterGeometryPackedBytes packed{};
        packed.metadataBytes.resize(sizeof(ClusterGeometryGpuHeader));
        packed.geometryBytes.resize(sizeof(ClusterGeometryGpuHeader));

        ClusterGeometryGpuHeader metadataHeader{};
        metadataHeader.flags = asset.flags;
        metadataHeader.surfaceCount = ClampToUint32(asset.surfaces.size());
        metadataHeader.surfaceLodRangeCount = ClampToUint32(asset.surfaceLodRanges.size());
        metadataHeader.surfaceSectionCount = ClampToUint32(asset.surfaceSections.size());
        metadataHeader.clusterCount = ClampToUint32(asset.clusters.size());
        metadataHeader.pageCount = ClampToUint32(asset.pages.size());
        metadataHeader.vertexCount = ClampToUint32(asset.packedVertices.size());
        metadataHeader.indexCount = options.includeFallbackIndices
            ? ClampToUint32(asset.packedIndices.size())
            : 0u;
        metadataHeader.materialSlotCount = ClampToUint32(asset.materialSlotMapping.size());
        metadataHeader.meshletPrimitiveCount = ClampToUint32(asset.meshletPrimitives.size());
        metadataHeader.totalTriangleCount = asset.totalTriangleCount;
        metadataHeader.totalVertexCount = asset.totalVertexCount;
        metadataHeader.localBoundsMin = BoundsMin4(asset.localBounds);
        metadataHeader.localBoundsMax = BoundsMax4(asset.localBounds);

        ClusterGeometryGpuHeader geometryHeader = metadataHeader;

        metadataHeader.surfaceOffsetBytes = AlignSection(packed.metadataBytes);
        packed.surfaceRanges.reserve(asset.surfaces.size());
        for (size_t surfaceIndex = 0; surfaceIndex < asset.surfaces.size(); ++surfaceIndex) {
            const ClusterSurface& surface = asset.surfaces[surfaceIndex];
            AppendPod(packed.metadataBytes, ToGpuSurface(surface));
            packed.surfaceRanges.push_back(ToSurfaceRange(ClampToUint32(surfaceIndex), surface));
        }

        metadataHeader.surfaceLodRangeOffsetBytes = AlignSection(packed.metadataBytes);
        packed.surfaceLodRanges.reserve(asset.surfaceLodRanges.size());
        for (const ClusterSurfaceLodRange& lodRange : asset.surfaceLodRanges) {
            AppendPod(packed.metadataBytes, ToGpuSurfaceLodRange(lodRange));
            packed.surfaceLodRanges.push_back(ToSurfaceLodRange(lodRange));
        }

        metadataHeader.surfaceSectionOffsetBytes = AlignSection(packed.metadataBytes);
        packed.surfaceSections.reserve(asset.surfaceSections.size());
        for (const ClusterSurfaceSection& section : asset.surfaceSections) {
            AppendPod(packed.metadataBytes, ToGpuSurfaceSection(section));
            packed.surfaceSections.push_back(ToSurfaceSection(section));
        }

        metadataHeader.clusterOffsetBytes = AlignSection(packed.metadataBytes);
        for (const MeshCluster& cluster : asset.clusters) {
            AppendPod(packed.metadataBytes, ToGpuCluster(cluster));
        }

        metadataHeader.pageOffsetBytes = AlignSection(packed.metadataBytes);
        for (const ClusterPage& page : asset.pages) {
            AppendPod(packed.metadataBytes, ToGpuPage(page));
        }

        geometryHeader.vertexOffsetBytes = AlignSection(packed.geometryBytes);
        for (const ClusterVertex& vertex : asset.packedVertices) {
            AppendPod(packed.geometryBytes, ToGpuVertexPosition(vertex));
        }

        // 属性列は position 列の直後に隙間なく続ける。HLSL 側は
        // vertexOffsetBytes + vertexCount * POSITION_BYTES で属性先頭を導出する
        // (HikariClusterVertexAttributeOffset) ため、ここに padding を入れてはならない。
        for (const ClusterVertex& vertex : asset.packedVertices) {
            AppendPod(packed.geometryBytes, ToGpuVertexAttributes(vertex));
        }

        if (options.includeFallbackIndices) {
            geometryHeader.indexOffsetBytes = AlignSection(packed.geometryBytes);
            std::vector<uint16_t> gpuIndices(asset.packedIndices.size(), 0u);
            for (const MeshCluster& cluster : asset.clusters) {
                const uint32_t indexEnd = cluster.firstIndex + cluster.indexCount;
                if (indexEnd > asset.packedIndices.size()) {
                    continue;
                }
                for (uint32_t indexOffset = 0; indexOffset < cluster.indexCount; ++indexOffset) {
                    const uint32_t sourceIndex = cluster.firstIndex + indexOffset;
                    gpuIndices[sourceIndex] =
                        static_cast<uint16_t>(asset.packedIndices[sourceIndex] & 0xffffu);
                }
            }
            for (const uint16_t index : gpuIndices) {
                AppendPod(packed.geometryBytes, index);
            }
        }

        geometryHeader.meshletPrimitiveOffsetBytes = AlignSection(packed.geometryBytes);
        for (const MeshletPrimitive& primitive : asset.meshletPrimitives) {
            AppendPod(packed.geometryBytes, ToGpuMeshletPrimitive(primitive));
        }

        geometryHeader.materialSlotOffsetBytes = AlignSection(packed.geometryBytes);
        for (const uint32_t materialSlot : asset.materialSlotMapping) {
            AppendPod(packed.geometryBytes, materialSlot);
        }

        packed.metadataBytes.resize(AlignUp(
            ClampToUint32(packed.metadataBytes.size()),
            kClusterGeometryGpuSectionAlignment));
        packed.geometryBytes.resize(AlignUp(
            ClampToUint32(packed.geometryBytes.size()),
            kClusterGeometryGpuSectionAlignment));

        metadataHeader.vertexOffsetBytes = geometryHeader.vertexOffsetBytes;
        metadataHeader.indexOffsetBytes = geometryHeader.indexOffsetBytes;
        metadataHeader.materialSlotOffsetBytes = geometryHeader.materialSlotOffsetBytes;
        metadataHeader.meshletPrimitiveOffsetBytes = geometryHeader.meshletPrimitiveOffsetBytes;
        metadataHeader.byteSize = ClampToUint32(packed.metadataBytes.size());

        geometryHeader.surfaceOffsetBytes = metadataHeader.surfaceOffsetBytes;
        geometryHeader.surfaceLodRangeOffsetBytes = metadataHeader.surfaceLodRangeOffsetBytes;
        geometryHeader.surfaceSectionOffsetBytes = metadataHeader.surfaceSectionOffsetBytes;
        geometryHeader.clusterOffsetBytes = metadataHeader.clusterOffsetBytes;
        geometryHeader.pageOffsetBytes = metadataHeader.pageOffsetBytes;
        geometryHeader.byteSize = ClampToUint32(packed.geometryBytes.size());

        std::memcpy(packed.metadataBytes.data(), &metadataHeader, sizeof(metadataHeader));
        std::memcpy(packed.geometryBytes.data(), &geometryHeader, sizeof(geometryHeader));

        packed.layout.surfaceCount = metadataHeader.surfaceCount;
        packed.layout.surfaceLodRangeCount = metadataHeader.surfaceLodRangeCount;
        packed.layout.surfaceSectionCount = metadataHeader.surfaceSectionCount;
        packed.layout.clusterCount = metadataHeader.clusterCount;
        packed.layout.pageCount = metadataHeader.pageCount;
        packed.layout.vertexCount = geometryHeader.vertexCount;
        packed.layout.indexCount = geometryHeader.indexCount;
        packed.layout.materialSlotCount = geometryHeader.materialSlotCount;
        packed.layout.surfaceOffsetBytes = metadataHeader.surfaceOffsetBytes;
        packed.layout.surfaceLodRangeOffsetBytes = metadataHeader.surfaceLodRangeOffsetBytes;
        packed.layout.surfaceSectionOffsetBytes = metadataHeader.surfaceSectionOffsetBytes;
        packed.layout.clusterOffsetBytes = metadataHeader.clusterOffsetBytes;
        packed.layout.pageOffsetBytes = metadataHeader.pageOffsetBytes;
        packed.layout.vertexOffsetBytes = geometryHeader.vertexOffsetBytes;
        packed.layout.indexOffsetBytes = geometryHeader.indexOffsetBytes;
        packed.layout.materialSlotOffsetBytes = geometryHeader.materialSlotOffsetBytes;
        packed.layout.meshletPrimitiveCount = geometryHeader.meshletPrimitiveCount;
        packed.layout.meshletPrimitiveOffsetBytes = geometryHeader.meshletPrimitiveOffsetBytes;
        packed.layout.totalTriangleCount = metadataHeader.totalTriangleCount;
        packed.layout.totalVertexCount = metadataHeader.totalVertexCount;
        packed.layout.flags = metadataHeader.flags;
        packed.layout.byteSize = geometryHeader.byteSize;
        packed.layout.localBoundsMin = metadataHeader.localBoundsMin;
        packed.layout.localBoundsMax = metadataHeader.localBoundsMax;
        packed.metadataByteSize = metadataHeader.byteSize;

        outPacked = std::move(packed);
        if (outMessage != nullptr) {
            *outMessage = "[ClusterGeometryPack] packed GPU-ready cluster geometry";
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
