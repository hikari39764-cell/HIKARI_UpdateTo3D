#ifndef HIKARI_MESHLET_DRAW_INCLUDED
#define HIKARI_MESHLET_DRAW_INCLUDED

struct HikariMeshletVisibleRange
{
    uint gpuSceneInstanceIndex;
    uint clusterGeometrySrvDescriptorIndex;
    uint firstCluster;
    uint clusterCount;
    uint clusterSurfaceIndex;
    uint passKind;
    uint flags;
    uint clusterGeometryMetadataSrvDescriptorIndex;
    uint lodIndex;
    uint pageIndex;
    uint drawBucket;
    uint sectionIndex;
    uint clusterOffsetBytes;
    uint vertexOffsetBytes;
    uint vertexCount;
    uint meshletPrimitiveOffsetBytes;
    uint meshletPrimitiveCount;
    uint geometryClusterCount;
    uint reserved0;
    uint reserved1;
    uint4 packetClusterIndices0;
    uint4 packetClusterIndices1;
    uint4 packetClusterIndices2;
    uint4 packetClusterIndices3;
};

static const uint HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PACKET = 1u;
static const uint HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PRECULLED = 2u;
static const uint HIKARI_MESHLET_VISIBLE_RANGE_FLAG_CLUSTER_LIST = 4u;
static const uint HIKARI_MESHLET_VISIBLE_PACKET_CAPACITY = 16u;
static const uint HIKARI_MESHLET_VISIBLE_CLUSTER_LIST_CAPACITY = 64u;

bool HikariMeshletVisibleRangeUsesPacket(HikariMeshletVisibleRange visible)
{
    return (visible.reserved0 & HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PACKET) != 0u;
}

bool HikariMeshletVisibleRangeIsPreculled(HikariMeshletVisibleRange visible)
{
    return (visible.reserved0 & HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PRECULLED) != 0u;
}

bool HikariMeshletVisibleRangeUsesClusterList(HikariMeshletVisibleRange visible)
{
    return (visible.reserved0 & HIKARI_MESHLET_VISIBLE_RANGE_FLAG_CLUSTER_LIST) != 0u;
}

uint HikariMeshletVisibleRangePacketCount(HikariMeshletVisibleRange visible)
{
    return min(visible.reserved1, HIKARI_MESHLET_VISIBLE_PACKET_CAPACITY);
}

uint HikariMeshletVisibleRangeClusterListCount(HikariMeshletVisibleRange visible)
{
    return min(visible.reserved1, HIKARI_MESHLET_VISIBLE_CLUSTER_LIST_CAPACITY);
}

uint HikariMeshletVisibleRangeClusterListStart(HikariMeshletVisibleRange visible)
{
    return visible.packetClusterIndices0.x;
}

uint HikariMeshletVisibleRangePacketIndex(HikariMeshletVisibleRange visible, uint slot)
{
    if (slot < 4u)
    {
        return visible.packetClusterIndices0[slot];
    }
    if (slot < 8u)
    {
        return visible.packetClusterIndices1[slot - 4u];
    }
    if (slot < 12u)
    {
        return visible.packetClusterIndices2[slot - 8u];
    }
    if (slot < 16u)
    {
        return visible.packetClusterIndices3[slot - 12u];
    }
    return 0xffffffffu;
}

cbuffer MeshletDrawControlCB : register(b8)
{
    uint gMeshletVisibleRangeIndex;
    uint gMeshletDrawPassKind;
    uint gMeshletCullBucket;
    uint gMeshletReserved0;
};

StructuredBuffer<HikariMeshletVisibleRange> gMeshletVisibleRanges : register(t18);
StructuredBuffer<uint> gMeshletVisibleClusterList : register(t19);

#endif
