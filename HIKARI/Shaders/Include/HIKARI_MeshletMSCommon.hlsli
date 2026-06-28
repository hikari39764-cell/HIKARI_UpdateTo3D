#ifndef HIKARI_MESHLET_MS_COMMON_INCLUDED
#define HIKARI_MESHLET_MS_COMMON_INCLUDED

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

#include "Include/HIKARI_MeshletDraw.hlsli"
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_CB 1
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_HELPERS 1
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/HIKARI_ClusterGpuData.hlsli"
#include "Include/HIKARI_RenderDescriptorLayout.hlsli"

static const uint HIKARI_CLUSTER_SRV_POOL_BEGIN = HIKARI_RENDER_SYSTEM_SRV_DYNAMIC_BEGIN;
static const uint HIKARI_CLUSTER_SRV_POOL_COUNT = HIKARI_RENDER_SYSTEM_SRV_DYNAMIC_COUNT;
static const uint HIKARI_MESHLET_MAX_PRIMITIVES =
    HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_PRIMITIVES;
static const uint HIKARI_MESHLET_MAX_VERTICES =
    HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_VERTICES;
static const uint HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD = 64u;
static const uint HIKARI_MESHLET_AS_MODE_COMPACT = 0u;
static const uint HIKARI_MESHLET_AS_MODE_DENSE = 1u;

ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

struct HikariMeshletPayload
{
    uint visibleRangeIndex;
    uint visibleClusterCount;
    uint clusterMode;
    uint reserved0;
    uint clusterOffsets[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
};

struct HikariMeshletResolvedCluster
{
    bool valid;
    uint clusterGeometryPoolIndex;
    uint clusterMetadataPoolIndex;
    uint clusterIndex;
    uint vertexCount;
    uint primitiveCount;
    HikariMeshletVisibleRange visible;
    HikariClusterGeometryHeader header;
    HikariMeshCluster cluster;
};

HikariMeshletVisibleRange HikariLoadMeshletVisibleRange(uint visibleRangeIndex)
{
    return gMeshletVisibleRanges[visibleRangeIndex];
}

HikariClusterGeometryHeader HikariBuildMeshletHeader(HikariMeshletVisibleRange visible)
{
    HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
    header.clusterOffsetBytes = visible.clusterOffsetBytes;
    header.vertexOffsetBytes = visible.vertexOffsetBytes;
    header.vertexCount = visible.vertexCount;
    header.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
    header.meshletPrimitiveCount = visible.meshletPrimitiveCount;
    header.clusterCount = visible.geometryClusterCount;
    return header;
}

uint HikariResolveMeshletClusterIndex(
    HikariMeshletVisibleRange visible,
    HikariMeshletPayload payload,
    uint groupIndex,
    out bool valid)
{
    const bool clusterListRange = HikariMeshletVisibleRangeUsesClusterList(visible);
    const bool packetRange = HikariMeshletVisibleRangeUsesPacket(visible);
    valid = true;

    if (clusterListRange)
    {
        uint listSlot =
            groupIndex < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD
                ? payload.clusterOffsets[groupIndex]
                : 0xffffffffu;
        const uint listCount = HikariMeshletVisibleRangeClusterListCount(visible);
        const uint listStart = HikariMeshletVisibleRangeClusterListStart(visible);
        valid = listSlot < listCount;
        return valid
            ? gMeshletVisibleClusterList[listStart + listSlot]
            : 0xffffffffu;
    }

    if (packetRange)
    {
        uint packetSlot =
            groupIndex < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD
                ? payload.clusterOffsets[groupIndex]
                : 0xffffffffu;
        uint packetCount = HikariMeshletVisibleRangePacketCount(visible);
        valid = packetSlot < packetCount;
        return valid
            ? HikariMeshletVisibleRangePacketIndex(visible, packetSlot)
            : 0xffffffffu;
    }

    uint localClusterOffset =
        payload.clusterMode == HIKARI_MESHLET_AS_MODE_DENSE
            ? groupIndex
            : (groupIndex < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD
                ? payload.clusterOffsets[groupIndex]
                : 0xffffffffu);
    valid = localClusterOffset < visible.clusterCount;
    return valid ? visible.firstCluster + localClusterOffset : 0xffffffffu;
}

HikariMeshletResolvedCluster HikariResolveMeshletCluster(
    HikariMeshletPayload payload,
    uint3 groupId)
{
    HikariMeshletResolvedCluster result = (HikariMeshletResolvedCluster)0;
    result.visible = HikariLoadMeshletVisibleRange(payload.visibleRangeIndex);

    bool valid =
        payload.visibleClusterCount != 0u &&
        groupId.x < payload.visibleClusterCount &&
        result.visible.clusterGeometrySrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN &&
        result.visible.clusterGeometryMetadataSrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN;

    result.clusterGeometryPoolIndex =
        valid
            ? result.visible.clusterGeometrySrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
            : 0u;
    result.clusterMetadataPoolIndex =
        valid
            ? result.visible.clusterGeometryMetadataSrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
            : 0u;
    valid =
        valid &&
        result.clusterGeometryPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT &&
        result.clusterMetadataPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT;
    result.clusterGeometryPoolIndex =
        min(result.clusterGeometryPoolIndex, HIKARI_CLUSTER_SRV_POOL_COUNT - 1u);
    result.clusterMetadataPoolIndex =
        min(result.clusterMetadataPoolIndex, HIKARI_CLUSTER_SRV_POOL_COUNT - 1u);
    result.header = HikariBuildMeshletHeader(result.visible);

    bool clusterIndexValid = false;
    result.clusterIndex =
        HikariResolveMeshletClusterIndex(
            result.visible,
            payload,
            groupId.x,
            clusterIndexValid);
    valid =
        valid &&
        clusterIndexValid &&
        result.header.clusterOffsetBytes != 0u &&
        result.header.vertexOffsetBytes != 0u &&
        result.header.meshletPrimitiveOffsetBytes != 0u &&
        result.clusterIndex < result.header.clusterCount;

    ByteAddressBuffer metadata =
        gClusterGeometryPool[NonUniformResourceIndex(result.clusterMetadataPoolIndex)];
    result.cluster =
        HikariLoadMeshCluster(
            metadata,
            result.header,
            valid ? result.clusterIndex : 0u);

    result.vertexCount = min(result.cluster.vertexCount, HIKARI_MESHLET_MAX_VERTICES);
    result.primitiveCount = min(result.cluster.triangleCount, HIKARI_MESHLET_MAX_PRIMITIVES);
    if (!valid ||
        result.cluster.vertexCount == 0u ||
        result.cluster.firstPrimitive >= result.header.meshletPrimitiveCount)
    {
        result.vertexCount = 0u;
        result.primitiveCount = 0u;
    }
    else
    {
        result.primitiveCount =
            min(
                result.primitiveCount,
                result.header.meshletPrimitiveCount - result.cluster.firstPrimitive);
    }
    result.valid = valid && result.vertexCount != 0u && result.primitiveCount != 0u;
    return result;
}

bool HikariResolveMeshletVertexIndex(
    HikariMeshletResolvedCluster resolved,
    uint localVertexIndex,
    out uint vertexIndex)
{
    vertexIndex = 0u;
    if (localVertexIndex >= resolved.cluster.vertexCount)
    {
        return false;
    }

    vertexIndex = resolved.cluster.firstVertex + localVertexIndex;
    return vertexIndex < resolved.header.vertexCount;
}

uint3 HikariLoadResolvedMeshletPrimitive(
    ByteAddressBuffer geometry,
    HikariMeshletResolvedCluster resolved,
    uint localPrimitiveIndex)
{
    return HikariLoadMeshletPrimitiveIndices(
        geometry,
        resolved.header,
        resolved.cluster.firstPrimitive + localPrimitiveIndex);
}

#endif
