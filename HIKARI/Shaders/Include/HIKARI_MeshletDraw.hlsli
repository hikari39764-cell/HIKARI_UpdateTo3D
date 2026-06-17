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
    uint clusterIndex;
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
};

cbuffer MeshletDrawControlCB : register(b8)
{
    uint gMeshletVisibleRangeIndex;
    uint gMeshletDrawPassKind;
    uint gMeshletCullBucket;
    uint gMeshletReserved0;
};

StructuredBuffer<HikariMeshletVisibleRange> gMeshletVisibleRanges : register(t18);

#endif
