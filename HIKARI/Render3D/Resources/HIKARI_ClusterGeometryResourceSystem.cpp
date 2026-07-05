#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3dx12.h>

#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"

namespace HIKARI::RENDER3D {

    namespace {
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

        uint64_t PackHandle(ClusterGeometryResourceHandle handle) {
            return (static_cast<uint64_t>(handle.generation) << 32) |
                static_cast<uint64_t>(handle.index);
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> CreateClusterGeometryGpuBuffer(
            const GFX::Context& context,
            const std::vector<uint8_t>& bytes) {

            if (context.device == nullptr ||
                context.cmdList == nullptr ||
                context.deferredReleaseQueue == nullptr ||
                bytes.empty()) {
                return {};
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> gpuBuffer{};
            const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
            const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bytes.size());
            HRESULT hr = context.device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(gpuBuffer.GetAddressOf()));
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Create default GPU buffer");
                return {};
            }
            GFX::SetD3D12Name(gpuBuffer.Get(), L"Cluster Geometry GPU Buffer");

            Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer{};
            const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            hr = context.device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadBuffer.GetAddressOf()));
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Create upload staging buffer");
                return {};
            }
            GFX::SetD3D12Name(uploadBuffer.Get(), L"Cluster Geometry Upload Staging");

            void* mapped = nullptr;
            hr = uploadBuffer->Map(0, nullptr, &mapped);
            if (FAILED(hr) || mapped == nullptr) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Map upload staging buffer");
                return {};
            }
            std::memcpy(mapped, bytes.data(), bytes.size());
            uploadBuffer->Unmap(0, nullptr);

            const CD3DX12_RESOURCE_BARRIER toCopyDest =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    gpuBuffer.Get(),
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_COPY_DEST);
            context.cmdList->ResourceBarrier(1, &toCopyDest);

            context.cmdList->CopyBufferRegion(
                gpuBuffer.Get(),
                0,
                uploadBuffer.Get(),
                0,
                static_cast<UINT64>(bytes.size()));

            const CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                gpuBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_GENERIC_READ);
            context.cmdList->ResourceBarrier(1, &barrier);

            Microsoft::WRL::ComPtr<ID3D12Resource> stagingKeepAlive = uploadBuffer;
            context.deferredReleaseQueue->Enqueue(
                context.currentFrameRetireFenceValue,
                [stagingKeepAlive]() mutable {
                    stagingKeepAlive.Reset();
                },
                "Cluster Geometry Upload Staging");

            return gpuBuffer;
        }

        RenderResourceDesc BuildResourceDesc(
            const std::string& sourceKey,
            const std::filesystem::path& hcmeshPath,
            const CLUSTER::ClusterGeometryPackedBytes& packed) {

            RenderResourceDesc desc{};
            desc.kind = RenderResourceKind::ClusterGeometry;
            desc.usage =
                RenderResourceUsageFlags::ShaderResource |
                RenderResourceUsageFlags::CopyDest |
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
            uint64_t byteSize) {

            if (resource == nullptr || byteSize == 0) {
                return {};
            }

            return AllocateBufferSrvDescriptor(
                resource,
                DXGI_FORMAT_R32_TYPELESS,
                static_cast<UINT>(byteSize / sizeof(uint32_t)),
                0,
                D3D12_BUFFER_SRV_FLAG_RAW);
        }

        void ReleaseClusterGeometryRecordViews(const ClusterGeometryResourceRecord& record) {
            if (record.srv.IsValid()) {
                ReleaseRenderResourceDescriptor(record.srv);
            }
            if (record.metadataSrv.IsValid()) {
                ReleaseRenderResourceDescriptor(record.metadataSrv);
            }
            if (record.metadataBuffer && State().context.deferredReleaseQueue != nullptr) {
                Microsoft::WRL::ComPtr<ID3D12Resource> keepAlive = record.metadataBuffer;
                State().context.deferredReleaseQueue->Enqueue(
                    State().context.currentFrameRetireFenceValue,
                    [keepAlive]() mutable {
                        keepAlive.Reset();
                    },
                    "Cluster Geometry Metadata Buffer");
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
                if (record.srv.IsValid() && record.metadataSrv.IsValid()) {
                    ++stats.shaderVisibleResourceCount;
                }
                else {
                    ++stats.missingDescriptorCount;
                }
                stats.surfaceCount += record.layout.surfaceCount;
                stats.surfaceLodRangeCount += record.layout.surfaceLodRangeCount;
                stats.surfaceSectionCount += record.layout.surfaceSectionCount;
                stats.clusterCount += record.layout.clusterCount;
                stats.pageCount += record.layout.pageCount;
                stats.vertexCount += record.layout.vertexCount;
                stats.indexCount += record.layout.indexCount;
                stats.meshletPrimitiveCount += record.layout.meshletPrimitiveCount;
                stats.surfaceRangeCount += ClampToUint32(record.surfaceRanges.size());
                stats.gpuBufferBytes += record.layout.byteSize + record.metadataBufferBytes;
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

        CLUSTER::ClusterGeometryPackedBytes packed{};
        std::string readMessage{};
        if (!ASSETS::GEOMETRY::ReadHcmeshPackedFile(hcmeshPath, packed, readMessage)) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> geometryBuffer =
            CreateClusterGeometryGpuBuffer(state.context, packed.geometryBytes);
        Microsoft::WRL::ComPtr<ID3D12Resource> metadataBuffer =
            CreateClusterGeometryGpuBuffer(state.context, packed.metadataBytes);
        if (!geometryBuffer || !metadataBuffer) {
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

        RenderResourceView srv = CreateClusterGeometrySrv(geometryBuffer.Get(), packed.layout.byteSize);
        RenderResourceView metadataSrv = CreateClusterGeometrySrv(metadataBuffer.Get(), packed.metadataByteSize);
        if (!srv.IsValid() || !metadataSrv.IsValid()) {
            if (srv.IsValid()) {
                ReleaseRenderResourceDescriptor(srv);
            }
            if (metadataSrv.IsValid()) {
                ReleaseRenderResourceDescriptor(metadataSrv);
            }
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            RebuildStats();
            return {};
        }

        if (!GetRenderResourcePool().AttachOwnedResource(
            handle.ToUntyped(),
            std::move(geometryBuffer),
            BuildResourceDesc(sourceKey, hcmeshPath, packed))) {
            ReleaseRenderResourceDescriptor(srv);
            ReleaseRenderResourceDescriptor(metadataSrv);
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        if (!GetRenderResourcePool().SetView(handle.ToUntyped(), RenderResourceViewKind::Srv, srv)) {
            ReleaseRenderResourceDescriptor(srv);
            ReleaseRenderResourceDescriptor(metadataSrv);
            const auto staleRecordIt = state.recordsBySourceKey.find(sourceKey);
            if (staleRecordIt != state.recordsBySourceKey.end()) {
                ReleaseClusterGeometryRecordViews(staleRecordIt->second);
                staleRecordIt->second.srv = {};
                staleRecordIt->second.metadataSrv = {};
                staleRecordIt->second.metadataBuffer.Reset();
                staleRecordIt->second.metadataBufferBytes = 0;
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
        record.surfaceLodRanges = packed.surfaceLodRanges;
        record.surfaceSections = packed.surfaceSections;
        record.srv = srv;
        record.metadataSrv = metadataSrv;
        record.metadataBuffer = std::move(metadataBuffer);
        record.metadataBufferBytes = packed.metadataByteSize;
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

    const CLUSTER::ClusterGeometrySurfaceLodRange* FindClusterGeometrySurfaceLodRange(
        ClusterGeometryResourceHandle handle,
        uint32_t firstLodRange,
        uint32_t lodRangeCount,
        uint32_t lodIndex) {

        const ClusterGeometryResourceRecord* record = GetClusterGeometryResource(handle);
        if (record == nullptr ||
            !record->ready ||
            lodRangeCount == 0u ||
            firstLodRange >= record->surfaceLodRanges.size()) {
            return nullptr;
        }

        const uint64_t begin = firstLodRange;
        const uint64_t end = (std::min)(
            begin + static_cast<uint64_t>(lodRangeCount),
            static_cast<uint64_t>(record->surfaceLodRanges.size()));
        for (uint64_t i = begin; i < end; ++i) {
            const CLUSTER::ClusterGeometrySurfaceLodRange& range =
                record->surfaceLodRanges[static_cast<size_t>(i)];
            if (range.lodIndex == lodIndex) {
                return &range;
            }
        }
        return nullptr;
    }

    ClusterGeometryResourceSystemStats GetClusterGeometryResourceSystemStats() {
        RebuildStats();
        return State().stats;
    }

} // namespace HIKARI::RENDER3D
