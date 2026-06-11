#ifndef HIKARI_CLUSTER_GPU_DATA_INCLUDED
#define HIKARI_CLUSTER_GPU_DATA_INCLUDED

static const uint HIKARI_CLUSTER_GEOMETRY_GPU_MAGIC = 0x534c4348u;
static const uint HIKARI_CLUSTER_GEOMETRY_GPU_VERSION = 2u;
static const uint HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX = 0xffffffffu;
static const uint HIKARI_CLUSTER_GEOMETRY_HEADER_BYTES = 112u;
static const uint HIKARI_CLUSTER_GEOMETRY_SURFACE_BYTES = 96u;
static const uint HIKARI_CLUSTER_GEOMETRY_CLUSTER_BYTES = 96u;
static const uint HIKARI_CLUSTER_GEOMETRY_PAGE_BYTES = 64u;
static const uint HIKARI_CLUSTER_GEOMETRY_VERTEX_BYTES = 80u;

struct HikariClusterGeometryHeader
{
    uint magic;
    uint version;
    uint flags;
    uint byteSize;

    uint surfaceCount;
    uint clusterCount;
    uint pageCount;
    uint vertexCount;

    uint indexCount;
    uint materialSlotCount;
    uint totalTriangleCount;
    uint totalVertexCount;

    uint surfaceOffsetBytes;
    uint clusterOffsetBytes;
    uint pageOffsetBytes;
    uint vertexOffsetBytes;

    uint indexOffsetBytes;
    uint materialSlotOffsetBytes;
    uint reserved0;
    uint reserved1;

    float4 localBoundsMin;
    float4 localBoundsMax;
};

struct HikariClusterGeometrySurface
{
    uint nodeIndex;
    uint meshIndex;
    uint primitiveIndex;
    uint materialIndex;

    uint firstCluster;
    uint clusterCount;
    uint firstIndex;
    uint indexCount;

    uint firstVertex;
    uint vertexCount;
    uint flags;
    uint firstPage;

    uint pageCount;
    uint reserved0;
    uint reserved1;
    uint reserved2;

    float4 boundsMin;
    float4 boundsMax;
};

struct HikariMeshCluster
{
    uint surfaceIndex;
    uint firstIndex;
    uint indexCount;
    uint firstVertex;

    uint vertexCount;
    uint triangleCount;
    uint flags;
    uint reserved0;

    float4 boundsMin;
    float4 boundsMax;
    float4 sphereCenterRadius;
    float4 coneAxisCutoff;
};

struct HikariClusterPage
{
    uint firstCluster;
    uint clusterCount;
    uint firstIndex;
    uint indexCount;

    uint firstVertex;
    uint vertexCount;
    uint reserved0;
    uint reserved1;

    float4 boundsMin;
    float4 boundsMax;
};

struct HikariClusterVertex
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 uv01;
    float4 color;
};

bool HikariIsValidClusterGeometryHeader(HikariClusterGeometryHeader header)
{
    return
        header.magic == HIKARI_CLUSTER_GEOMETRY_GPU_MAGIC &&
        header.version == HIKARI_CLUSTER_GEOMETRY_GPU_VERSION &&
        header.byteSize > 0;
}

HikariClusterGeometryHeader HikariLoadClusterGeometryHeader(ByteAddressBuffer buffer)
{
    HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
    uint4 v0 = buffer.Load4(0u);
    uint4 v1 = buffer.Load4(16u);
    uint4 v2 = buffer.Load4(32u);
    uint4 v3 = buffer.Load4(48u);
    uint4 v4 = buffer.Load4(64u);
    header.magic = v0.x;
    header.version = v0.y;
    header.flags = v0.z;
    header.byteSize = v0.w;
    header.surfaceCount = v1.x;
    header.clusterCount = v1.y;
    header.pageCount = v1.z;
    header.vertexCount = v1.w;
    header.indexCount = v2.x;
    header.materialSlotCount = v2.y;
    header.totalTriangleCount = v2.z;
    header.totalVertexCount = v2.w;
    header.surfaceOffsetBytes = v3.x;
    header.clusterOffsetBytes = v3.y;
    header.pageOffsetBytes = v3.z;
    header.vertexOffsetBytes = v3.w;
    header.indexOffsetBytes = v4.x;
    header.materialSlotOffsetBytes = v4.y;
    header.reserved0 = v4.z;
    header.reserved1 = v4.w;
    header.localBoundsMin = asfloat(buffer.Load4(80u));
    header.localBoundsMax = asfloat(buffer.Load4(96u));
    return header;
}

HikariClusterGeometrySurface HikariLoadClusterGeometrySurface(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint surfaceIndex)
{
    HikariClusterGeometrySurface surface = (HikariClusterGeometrySurface)0;
    uint offset = header.surfaceOffsetBytes + surfaceIndex * HIKARI_CLUSTER_GEOMETRY_SURFACE_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    uint4 v2 = buffer.Load4(offset + 32u);
    uint4 v3 = buffer.Load4(offset + 48u);
    surface.nodeIndex = v0.x;
    surface.meshIndex = v0.y;
    surface.primitiveIndex = v0.z;
    surface.materialIndex = v0.w;
    surface.firstCluster = v1.x;
    surface.clusterCount = v1.y;
    surface.firstIndex = v1.z;
    surface.indexCount = v1.w;
    surface.firstVertex = v2.x;
    surface.vertexCount = v2.y;
    surface.flags = v2.z;
    surface.firstPage = v2.w;
    surface.pageCount = v3.x;
    surface.reserved0 = v3.y;
    surface.reserved1 = v3.z;
    surface.reserved2 = v3.w;
    surface.boundsMin = asfloat(buffer.Load4(offset + 64u));
    surface.boundsMax = asfloat(buffer.Load4(offset + 80u));
    return surface;
}

HikariMeshCluster HikariLoadMeshCluster(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint clusterIndex)
{
    HikariMeshCluster cluster = (HikariMeshCluster)0;
    uint offset = header.clusterOffsetBytes + clusterIndex * HIKARI_CLUSTER_GEOMETRY_CLUSTER_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    cluster.surfaceIndex = v0.x;
    cluster.firstIndex = v0.y;
    cluster.indexCount = v0.z;
    cluster.firstVertex = v0.w;
    cluster.vertexCount = v1.x;
    cluster.triangleCount = v1.y;
    cluster.flags = v1.z;
    cluster.reserved0 = v1.w;
    cluster.boundsMin = asfloat(buffer.Load4(offset + 32u));
    cluster.boundsMax = asfloat(buffer.Load4(offset + 48u));
    cluster.sphereCenterRadius = asfloat(buffer.Load4(offset + 64u));
    cluster.coneAxisCutoff = asfloat(buffer.Load4(offset + 80u));
    return cluster;
}

HikariClusterPage HikariLoadClusterPage(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint pageIndex)
{
    HikariClusterPage page = (HikariClusterPage)0;
    uint offset = header.pageOffsetBytes + pageIndex * HIKARI_CLUSTER_GEOMETRY_PAGE_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    page.firstCluster = v0.x;
    page.clusterCount = v0.y;
    page.firstIndex = v0.z;
    page.indexCount = v0.w;
    page.firstVertex = v1.x;
    page.vertexCount = v1.y;
    page.reserved0 = v1.z;
    page.reserved1 = v1.w;
    page.boundsMin = asfloat(buffer.Load4(offset + 32u));
    page.boundsMax = asfloat(buffer.Load4(offset + 48u));
    return page;
}

HikariClusterVertex HikariLoadClusterVertex(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    HikariClusterVertex vertex = (HikariClusterVertex)0;
    uint offset = header.vertexOffsetBytes + vertexIndex * HIKARI_CLUSTER_GEOMETRY_VERTEX_BYTES;
    vertex.position = asfloat(buffer.Load4(offset + 0u));
    vertex.normal = asfloat(buffer.Load4(offset + 16u));
    vertex.tangent = asfloat(buffer.Load4(offset + 32u));
    vertex.uv01 = asfloat(buffer.Load4(offset + 48u));
    vertex.color = asfloat(buffer.Load4(offset + 64u));
    return vertex;
}

uint HikariLoadClusterIndex(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint indexIndex)
{
    return buffer.Load(header.indexOffsetBytes + indexIndex * 4u);
}

#endif
