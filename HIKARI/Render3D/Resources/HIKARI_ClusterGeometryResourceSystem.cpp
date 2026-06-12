#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3dx12.h>

#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"
#include "Render3D/Resources/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"

namespace HIKARI::RENDER3D {

    namespace {
        using ClusterGeometryGpuHeader = CLUSTER::ClusterGeometryGpuHeader;
        using ClusterGeometryGpuSurface = CLUSTER::ClusterGeometryGpuSurface;
        using ClusterGeometryGpuCluster = CLUSTER::ClusterGeometryGpuCluster;
        using ClusterGeometryGpuPage = CLUSTER::ClusterGeometryGpuPage;
        using ClusterGeometryGpuVertex = CLUSTER::ClusterGeometryGpuVertex;
        using ClusterGeometryGpuMeshletPrimitive = CLUSTER::ClusterGeometryGpuMeshletPrimitive;
        using ClusterGeometrySurfaceRange = CLUSTER::ClusterGeometrySurfaceRange;

        struct PackedClusterGeometry {
            std::vector<uint8_t> bytes{};
            ClusterGeometryGpuLayout layout{};
            std::vector<ClusterGeometrySurfaceRange> surfaceRanges{};
        };

        struct ClusterGeometryResourceSystemState {
            GFX::Context context{};
            std::unordered_map<std::string, ClusterGeometryResourceRecord> recordsBySourceKey{};
            std::unordered_map<uint64_t, std::string> sourceKeyByHandle{};
            ClusterGeometryResourceSystemStats stats{};
        };

        ClusterGeometryResourceSystemState& State() {
            static ClusterGeometryResourceSystemState state{};
            return state;
        }

        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        uint32_t AlignUp(uint32_t value, uint32_t alignment) {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        uint64_t PackHandle(ClusterGeometryResourceHandle handle) {
            return (static_cast<uint64_t>(handle.generation) << 32) |
                static_cast<uint64_t>(handle.index);
        }

        MATH::Vec4 BoundsMin4(const Bounds& bounds) {
            return { bounds.min.x, bounds.min.y, bounds.min.z, 1.0f };
        }

        MATH::Vec4 BoundsMax4(const Bounds& bounds) {
            return { bounds.max.x, bounds.max.y, bounds.max.z, 1.0f };
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
            const uint32_t alignedOffset = AlignUp(offset, CLUSTER::kClusterGeometryGpuSectionAlignment);
            bytes.resize(alignedOffset);
            return alignedOffset;
        }

        ClusterGeometrySurfaceRange ToSurfaceRange(
            uint32_t surfaceIndex,
            const CLUSTER::ClusterSurface& source) {

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
            return range;
        }

        ClusterGeometryGpuSurface ToGpuSurface(const CLUSTER::ClusterSurface& source) {
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
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            return gpu;
        }

        ClusterGeometryGpuCluster ToGpuCluster(const CLUSTER::MeshCluster& source) {
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
            gpu.coneAxisCutoff = {
                source.coneAxis.x,
                source.coneAxis.y,
                source.coneAxis.z,
                source.coneCutoff
            };
            return gpu;
        }

        ClusterGeometryGpuPage ToGpuPage(const CLUSTER::ClusterPage& source) {
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
            const CLUSTER::MeshletPrimitive& source) {

            ClusterGeometryGpuMeshletPrimitive gpu{};
            gpu.i0 = source.i0;
            gpu.i1 = source.i1;
            gpu.i2 = source.i2;
            gpu.reserved0 = source.reserved0;
            return gpu;
        }

        ClusterGeometryGpuVertex ToGpuVertex(const CLUSTER::ClusterVertex& source) {
            ClusterGeometryGpuVertex gpu{};
            gpu.position = { source.position.x, source.position.y, source.position.z, 1.0f };
            gpu.normal = { source.normal.x, source.normal.y, source.normal.z, 0.0f };
            gpu.tangent = source.tangent;
            gpu.uv01 = { source.uv0.x, source.uv0.y, source.uv1.x, source.uv1.y };
            gpu.color = source.color;
            return gpu;
        }

        PackedClusterGeometry PackClusterGeometry(const CLUSTER::ClusteredGeometryAsset& asset) {
            PackedClusterGeometry packed{};
            packed.bytes.resize(sizeof(ClusterGeometryGpuHeader));

            ClusterGeometryGpuHeader header{};
            header.flags = asset.flags;
            header.surfaceCount = ClampToUint32(asset.surfaces.size());
            header.clusterCount = ClampToUint32(asset.clusters.size());
            header.pageCount = ClampToUint32(asset.pages.size());
            header.vertexCount = ClampToUint32(asset.packedVertices.size());
            header.indexCount = ClampToUint32(asset.packedIndices.size());
            header.materialSlotCount = ClampToUint32(asset.materialSlotMapping.size());
            header.meshletPrimitiveCount = ClampToUint32(asset.meshletPrimitives.size());
            header.totalTriangleCount = asset.totalTriangleCount;
            header.totalVertexCount = asset.totalVertexCount;
            header.localBoundsMin = BoundsMin4(asset.localBounds);
            header.localBoundsMax = BoundsMax4(asset.localBounds);

            header.surfaceOffsetBytes = AlignSection(packed.bytes);
            packed.surfaceRanges.reserve(asset.surfaces.size());
            for (size_t surfaceIndex = 0; surfaceIndex < asset.surfaces.size(); ++surfaceIndex) {
                const CLUSTER::ClusterSurface& surface = asset.surfaces[surfaceIndex];
                AppendPod(packed.bytes, ToGpuSurface(surface));
                packed.surfaceRanges.push_back(ToSurfaceRange(ClampToUint32(surfaceIndex), surface));
            }

            header.clusterOffsetBytes = AlignSection(packed.bytes);
            for (const CLUSTER::MeshCluster& cluster : asset.clusters) {
                AppendPod(packed.bytes, ToGpuCluster(cluster));
            }

            header.pageOffsetBytes = AlignSection(packed.bytes);
            for (const CLUSTER::ClusterPage& page : asset.pages) {
                AppendPod(packed.bytes, ToGpuPage(page));
            }

            header.vertexOffsetBytes = AlignSection(packed.bytes);
            for (const CLUSTER::ClusterVertex& vertex : asset.packedVertices) {
                AppendPod(packed.bytes, ToGpuVertex(vertex));
            }

            header.indexOffsetBytes = AlignSection(packed.bytes);
            // GPU側では cluster 局所 index ではなく、packed vertex への直接 index として扱う。
            std::vector<uint32_t> gpuIndices(asset.packedIndices.size(), 0u);
            for (const CLUSTER::MeshCluster& cluster : asset.clusters) {
                const uint32_t indexEnd = cluster.firstIndex + cluster.indexCount;
                if (indexEnd > asset.packedIndices.size()) {
                    continue;
                }
                for (uint32_t indexOffset = 0; indexOffset < cluster.indexCount; ++indexOffset) {
                    const uint32_t sourceIndex = cluster.firstIndex + indexOffset;
                    gpuIndices[sourceIndex] =
                        cluster.firstVertex + asset.packedIndices[sourceIndex];
                }
            }
            for (const uint32_t index : gpuIndices) {
                AppendPod(packed.bytes, index);
            }

            header.meshletPrimitiveOffsetBytes = AlignSection(packed.bytes);
            for (const CLUSTER::MeshletPrimitive& primitive : asset.meshletPrimitives) {
                AppendPod(packed.bytes, ToGpuMeshletPrimitive(primitive));
            }

            header.materialSlotOffsetBytes = AlignSection(packed.bytes);
            for (const uint32_t materialSlot : asset.materialSlotMapping) {
                AppendPod(packed.bytes, materialSlot);
            }

            packed.bytes.resize(AlignUp(
                ClampToUint32(packed.bytes.size()),
                CLUSTER::kClusterGeometryGpuSectionAlignment));
            header.byteSize = ClampToUint32(packed.bytes.size());
            std::memcpy(packed.bytes.data(), &header, sizeof(header));

            packed.layout.surfaceCount = header.surfaceCount;
            packed.layout.clusterCount = header.clusterCount;
            packed.layout.pageCount = header.pageCount;
            packed.layout.vertexCount = header.vertexCount;
            packed.layout.indexCount = header.indexCount;
            packed.layout.materialSlotCount = header.materialSlotCount;
            packed.layout.surfaceOffsetBytes = header.surfaceOffsetBytes;
            packed.layout.clusterOffsetBytes = header.clusterOffsetBytes;
            packed.layout.pageOffsetBytes = header.pageOffsetBytes;
            packed.layout.vertexOffsetBytes = header.vertexOffsetBytes;
            packed.layout.indexOffsetBytes = header.indexOffsetBytes;
            packed.layout.materialSlotOffsetBytes = header.materialSlotOffsetBytes;
            packed.layout.meshletPrimitiveCount = header.meshletPrimitiveCount;
            packed.layout.meshletPrimitiveOffsetBytes = header.meshletPrimitiveOffsetBytes;
            packed.layout.totalTriangleCount = header.totalTriangleCount;
            packed.layout.totalVertexCount = header.totalVertexCount;
            packed.layout.flags = header.flags;
            packed.layout.byteSize = header.byteSize;
            packed.layout.localBoundsMin = header.localBoundsMin;
            packed.layout.localBoundsMax = header.localBoundsMax;
            return packed;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(
            ID3D12Device* device,
            const std::vector<uint8_t>& bytes) {

            if (device == nullptr || bytes.empty()) {
                return {};
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
            const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
            const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bytes.size());
            if (FAILED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(buffer.GetAddressOf())))) {
                return {};
            }

            void* mapped = nullptr;
            if (FAILED(buffer->Map(0, nullptr, &mapped)) || mapped == nullptr) {
                return {};
            }
            std::memcpy(mapped, bytes.data(), bytes.size());
            buffer->Unmap(0, nullptr);
            GFX::SetD3D12Name(buffer.Get(), L"Cluster Geometry GPU Buffer");
            return buffer;
        }

        RenderResourceDesc BuildResourceDesc(
            const std::string& sourceKey,
            const std::filesystem::path& hcmeshPath,
            const PackedClusterGeometry& packed) {

            RenderResourceDesc desc{};
            desc.kind = RenderResourceKind::ClusterGeometry;
            desc.usage =
                RenderResourceUsageFlags::ShaderResource |
                RenderResourceUsageFlags::IndirectArgument;
            desc.lifetime = RenderResourceLifetime::ImportedAsset;
            desc.sourceKey = sourceKey;
            desc.debugName = "ClusterGeometry " + hcmeshPath.filename().generic_string();
            desc.byteSize = packed.layout.byteSize;
            desc.width = packed.layout.byteSize;
            desc.height = 1;
            desc.depthOrArraySize = 1;
            desc.mipLevels = 1;
            return desc;
        }

        RenderResourceView CreateClusterGeometrySrv(
            ID3D12Resource* resource,
            const PackedClusterGeometry& packed) {

            if (resource == nullptr || packed.layout.byteSize == 0) {
                return {};
            }

            return AllocateBufferSrvDescriptor(
                resource,
                DXGI_FORMAT_R32_TYPELESS,
                packed.layout.byteSize / sizeof(uint32_t),
                0,
                D3D12_BUFFER_SRV_FLAG_RAW);
        }

        void ReleaseClusterGeometryRecordViews(const ClusterGeometryResourceRecord& record) {
            if (record.srv.IsValid()) {
                ReleaseRenderResourceDescriptor(record.srv);
            }
        }

        ClusterGeometryResourceHandle FindOrRegisterHandle(
            const std::string& sourceKey,
            const std::filesystem::path& hcmeshPath) {

            RenderResourcePool& pool = GetRenderResourcePool();
            if (const RenderResourceHandle cached =
                pool.FindBySourceKey(RenderResourceKind::ClusterGeometry, sourceKey)) {
                return ClusterGeometryResourceHandle::FromUntyped(cached);
            }

            return RegisterVirtualClusterGeometryResource(
                sourceKey,
                "ClusterGeometry " + hcmeshPath.filename().generic_string());
        }

        void RebuildStats() {
            ClusterGeometryResourceSystemState& state = State();
            const uint32_t requests = state.stats.requestCount;
            const uint32_t hits = state.stats.hitCount;
            const uint32_t misses = state.stats.missCount;
            const uint32_t loaded = state.stats.loadedCount;
            const uint32_t failed = state.stats.failedCount;
            const uint32_t missingDevice = state.stats.missingDeviceCount;
            const uint32_t upgraded = state.stats.upgradedVirtualHandleCount;
            const uint32_t descriptorAllocationFailed =
                state.stats.descriptorAllocationFailedCount;

            ClusterGeometryResourceSystemStats stats{};
            stats.initialized = state.context.device != nullptr && state.context.srvHeap != nullptr;
            stats.requestCount = requests;
            stats.hitCount = hits;
            stats.missCount = misses;
            stats.loadedCount = loaded;
            stats.failedCount = failed;
            stats.missingDeviceCount = missingDevice;
            stats.upgradedVirtualHandleCount = upgraded;
            stats.descriptorAllocationFailedCount = descriptorAllocationFailed;
            stats.resourceCount = ClampToUint32(state.recordsBySourceKey.size());

            for (const auto& pair : state.recordsBySourceKey) {
                const ClusterGeometryResourceRecord& record = pair.second;
                if (!record.ready) {
                    continue;
                }
                ++stats.readyResourceCount;
                if (record.srv.IsValid()) {
                    ++stats.shaderVisibleResourceCount;
                }
                else {
                    ++stats.missingDescriptorCount;
                }
                stats.surfaceCount += record.layout.surfaceCount;
                stats.clusterCount += record.layout.clusterCount;
                stats.pageCount += record.layout.pageCount;
                stats.vertexCount += record.layout.vertexCount;
                stats.indexCount += record.layout.indexCount;
                stats.meshletPrimitiveCount += record.layout.meshletPrimitiveCount;
                stats.surfaceRangeCount += ClampToUint32(record.surfaceRanges.size());
                stats.gpuBufferBytes += record.layout.byteSize;
            }

            state.stats = stats;
        }
    } // namespace

    void UpdateClusterGeometryResourceContext(const GFX::Context& ctx) {
        State().context = ctx;
        RebuildStats();
    }

    void ShutdownClusterGeometryResourceSystem() {
        ClusterGeometryResourceSystemState& state = State();
        for (const auto& pair : state.recordsBySourceKey) {
            const ClusterGeometryResourceRecord& record = pair.second;
            ReleaseClusterGeometryRecordViews(record);
            if (record.handle) {
                GetRenderResourcePool().Release(record.handle.ToUntyped());
            }
        }
        state.recordsBySourceKey.clear();
        state.sourceKeyByHandle.clear();
        state.context = {};
        state.stats = {};
    }

    ClusterGeometryResourceHandle LoadClusterGeometryResource(
        const std::string& sourceKey,
        const std::filesystem::path& hcmeshPath) {

        ClusterGeometryResourceSystemState& state = State();
        ++state.stats.requestCount;

        if (sourceKey.empty() || hcmeshPath.empty()) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        const auto cached = state.recordsBySourceKey.find(sourceKey);
        if (cached != state.recordsBySourceKey.end() &&
            cached->second.ready &&
            GetRenderResourcePool().IsAlive(cached->second.handle.ToUntyped())) {
            ++state.stats.hitCount;
            RebuildStats();
            return cached->second.handle;
        }

        ++state.stats.missCount;
        if (state.context.device == nullptr) {
            ++state.stats.failedCount;
            ++state.stats.missingDeviceCount;
            RebuildStats();
            return {};
        }

        const CLUSTER::ClusteredGeometryAsset* asset =
            CLUSTER::GetClusteredGeometryManager().LoadOrGet(hcmeshPath);
        if (asset == nullptr || !asset->valid) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        const PackedClusterGeometry packed = PackClusterGeometry(*asset);
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer =
            CreateUploadBuffer(state.context.device, packed.bytes);
        if (!buffer) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        ClusterGeometryResourceHandle handle = FindOrRegisterHandle(sourceKey, hcmeshPath);
        if (!handle) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        RenderResourceView srv = CreateClusterGeometrySrv(buffer.Get(), packed);
        if (!srv.IsValid()) {
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            RebuildStats();
            return {};
        }

        if (!GetRenderResourcePool().AttachOwnedResource(
            handle.ToUntyped(),
            std::move(buffer),
            BuildResourceDesc(sourceKey, hcmeshPath, packed))) {
            ReleaseRenderResourceDescriptor(srv);
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        if (!GetRenderResourcePool().SetView(handle.ToUntyped(), RenderResourceViewKind::Srv, srv)) {
            ReleaseRenderResourceDescriptor(srv);
            const auto staleRecordIt = state.recordsBySourceKey.find(sourceKey);
            if (staleRecordIt != state.recordsBySourceKey.end()) {
                ReleaseClusterGeometryRecordViews(staleRecordIt->second);
                staleRecordIt->second.srv = {};
                staleRecordIt->second.ready = false;
            }
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            RebuildStats();
            return {};
        }

        const auto oldRecordIt = state.recordsBySourceKey.find(sourceKey);
        if (oldRecordIt != state.recordsBySourceKey.end()) {
            ReleaseClusterGeometryRecordViews(oldRecordIt->second);
            if (oldRecordIt->second.handle != handle) {
                state.sourceKeyByHandle.erase(PackHandle(oldRecordIt->second.handle));
            }
        }

        ClusterGeometryResourceRecord record{};
        record.handle = handle;
        record.sourceKey = sourceKey;
        record.sourcePath = hcmeshPath;
        record.layout = packed.layout;
        record.surfaceRanges = packed.surfaceRanges;
        record.srv = srv;
        record.ready = true;
        state.recordsBySourceKey[sourceKey] = std::move(record);
        state.sourceKeyByHandle[PackHandle(handle)] = sourceKey;

        ++state.stats.loadedCount;
        ++state.stats.upgradedVirtualHandleCount;
        RebuildStats();
        return handle;
    }

    const ClusterGeometryResourceRecord* GetClusterGeometryResource(
        ClusterGeometryResourceHandle handle) {

        if (!handle) {
            return nullptr;
        }

        ClusterGeometryResourceSystemState& state = State();
        const auto keyIt = state.sourceKeyByHandle.find(PackHandle(handle));
        if (keyIt == state.sourceKeyByHandle.end()) {
            return nullptr;
        }
        const auto recordIt = state.recordsBySourceKey.find(keyIt->second);
        return recordIt != state.recordsBySourceKey.end() ? &recordIt->second : nullptr;
    }

    const CLUSTER::ClusterGeometrySurfaceRange* FindClusterGeometrySurfaceRange(
        ClusterGeometryResourceHandle handle,
        uint32_t nodeIndex,
        uint32_t meshIndex,
        uint32_t primitiveIndex) {

        const ClusterGeometryResourceRecord* record = GetClusterGeometryResource(handle);
        if (record == nullptr || !record->ready) {
            return nullptr;
        }

        for (const CLUSTER::ClusterGeometrySurfaceRange& range : record->surfaceRanges) {
            if (range.nodeIndex == nodeIndex &&
                range.meshIndex == meshIndex &&
                range.primitiveIndex == primitiveIndex) {
                return &range;
            }
        }

        const CLUSTER::ClusterGeometrySurfaceRange* uniqueMeshPrimitiveRange = nullptr;
        for (const CLUSTER::ClusterGeometrySurfaceRange& range : record->surfaceRanges) {
            if (range.meshIndex != meshIndex || range.primitiveIndex != primitiveIndex) {
                continue;
            }
            if (uniqueMeshPrimitiveRange != nullptr) {
                return nullptr;
            }
            uniqueMeshPrimitiveRange = &range;
        }
        if (uniqueMeshPrimitiveRange != nullptr) {
            // node ID が一致しない旧 surface でも、mesh/primitive が一意なら同じ geometry range として扱う。
            return uniqueMeshPrimitiveRange;
        }

        // HCMESH は node transform を頂点へ bake しているため、node が違う surface は別ジオメトリとして扱う。
        return nullptr;
    }

    ClusterGeometryResourceSystemStats GetClusterGeometryResourceSystemStats() {
        RebuildStats();
        return State().stats;
    }

} // namespace HIKARI::RENDER3D
