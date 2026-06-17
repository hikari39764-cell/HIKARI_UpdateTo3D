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

static const uint HIKARI_CLUSTER_SRV_POOL_BEGIN = 3985u;
static const uint HIKARI_CLUSTER_SRV_POOL_COUNT = 111u;
static const uint HIKARI_MESHLET_MAX_PRIMITIVES =
    HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_PRIMITIVES;
static const uint HIKARI_MESHLET_MAX_VERTICES =
    HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_VERTICES;

ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

struct HikariMeshletPayload
{
    uint visibleRangeIndex;
};

struct HikariMeshletVertexOut
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    nointerpolation uint receiveShadow : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugClusterId : TEXCOORD6;
    nointerpolation uint debugSurfaceId : TEXCOORD7;
    nointerpolation uint debugLodIndex : TEXCOORD8;
    nointerpolation uint debugDrawBucket : TEXCOORD9;
};

HikariMeshletVertexOut HikariBuildEmptyMeshletVertex()
{
    HikariMeshletVertexOut output = (HikariMeshletVertexOut)0;
    output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    output.normalWS = float3(0.0f, 1.0f, 0.0f);
    output.tangentWS = float4(1.0f, 0.0f, 0.0f, 1.0f);
    return output;
}

bool HikariResolveMeshletVertex(
    ByteAddressBuffer geometry,
    HikariClusterGeometryHeader header,
    HikariMeshCluster cluster,
    uint localVertexIndex,
    out HikariClusterVertex vertex)
{
    vertex = (HikariClusterVertex)0;
    if (localVertexIndex >= cluster.vertexCount)
    {
        return false;
    }

    uint vertexIndex = cluster.firstVertex + localVertexIndex;
    if (vertexIndex >= header.vertexCount)
    {
        return false;
    }

    vertex = HikariLoadClusterVertex(geometry, header, vertexIndex);
    return true;
}

HikariMeshletVertexOut HikariBuildMeshletVertex(
    HikariSurfaceGpuSceneInstance instance,
    HikariMeshletVisibleRange visible,
    HikariClusterVertex vertex,
    uint surfaceGpuSceneIndex,
    uint clusterIndex)
{
    HikariMeshletVertexOut output = HikariBuildEmptyMeshletVertex();
    float4 worldPos = mul(instance.clusterWorld, float4(vertex.position.xyz, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)instance.clusterNormalMatrix, vertex.normal.xyz));
    output.tangentWS = float4(
        normalize(mul((float3x3)instance.clusterNormalMatrix, vertex.tangent.xyz)),
        vertex.tangent.w);
    output.uv = vertex.uv01.xy;
    output.materialDataIndex = instance.materialDataIndex;
    output.receiveShadow =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW) != 0u ? 1u : 0u;
    output.objectDataIndex = surfaceGpuSceneIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.debugClusterId = clusterIndex;
    output.debugSurfaceId = visible.clusterSurfaceIndex * 4099u + visible.sectionIndex;
    output.debugLodIndex = visible.lodIndex;
    output.debugDrawBucket = visible.drawBucket;
    return output;
}

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void main(
    uint groupIndex : SV_GroupIndex,
    uint3 groupId : SV_GroupID,
    in payload HikariMeshletPayload payload,
    out vertices HikariMeshletVertexOut vertices[HIKARI_MESHLET_MAX_VERTICES],
    out indices uint3 triangles[HIKARI_MESHLET_MAX_PRIMITIVES])
{
    HikariMeshletVisibleRange visible = gMeshletVisibleRanges[payload.visibleRangeIndex];
    bool valid =
        groupId.x < visible.clusterCount &&
        visible.clusterGeometrySrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN;
    uint clusterGeometryPoolIndex =
        valid
            ? visible.clusterGeometrySrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
            : 0u;
    valid = valid && clusterGeometryPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT;
    uint safeClusterGeometryPoolIndex =
        min(clusterGeometryPoolIndex, HIKARI_CLUSTER_SRV_POOL_COUNT - 1u);

    ByteAddressBuffer geometry =
        gClusterGeometryPool[NonUniformResourceIndex(safeClusterGeometryPoolIndex)];
    HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
    header.clusterOffsetBytes = visible.clusterOffsetBytes;
    header.vertexOffsetBytes = visible.vertexOffsetBytes;
    header.vertexCount = visible.vertexCount;
    header.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
    header.meshletPrimitiveCount = visible.meshletPrimitiveCount;
    header.clusterCount = visible.geometryClusterCount;
    uint clusterIndex = visible.firstCluster + groupId.x;
    valid =
        valid &&
        header.clusterOffsetBytes != 0u &&
        header.vertexOffsetBytes != 0u &&
        header.meshletPrimitiveOffsetBytes != 0u &&
        clusterIndex < header.clusterCount;
    uint safeClusterIndex = valid ? clusterIndex : 0u;

    HikariMeshCluster cluster = HikariLoadMeshCluster(geometry, header, safeClusterIndex);
    uint vertexCount = min(cluster.vertexCount, HIKARI_MESHLET_MAX_VERTICES);
    uint primitiveCount = min(cluster.triangleCount, HIKARI_MESHLET_MAX_PRIMITIVES);
    if (!valid ||
        cluster.vertexCount == 0u ||
        cluster.firstPrimitive >= header.meshletPrimitiveCount)
    {
        vertexCount = 0u;
        primitiveCount = 0u;
    }
    else
    {
        primitiveCount = min(
            primitiveCount,
            header.meshletPrimitiveCount - cluster.firstPrimitive);
    }

    SetMeshOutputCounts(vertexCount, primitiveCount);
    if (!valid)
    {
        return;
    }

    HikariSurfaceGpuSceneInstance instance =
        gSurfaceGpuSceneBuffer[visible.gpuSceneInstanceIndex];

    if (groupIndex < vertexCount)
    {
        HikariClusterVertex vertex;
        if (HikariResolveMeshletVertex(geometry, header, cluster, groupIndex, vertex))
        {
            vertices[groupIndex] =
                HikariBuildMeshletVertex(
                    instance,
                    visible,
                    vertex,
                    visible.gpuSceneInstanceIndex,
                    clusterIndex);
        }
        else
        {
            vertices[groupIndex] = HikariBuildEmptyMeshletVertex();
        }
    }

    if (groupIndex < primitiveCount)
    {
        HikariMeshletPrimitive primitive =
            HikariLoadMeshletPrimitive(
                geometry,
                header,
                cluster.firstPrimitive + groupIndex);
        if (primitive.i0 < vertexCount &&
            primitive.i1 < vertexCount &&
            primitive.i2 < vertexCount)
        {
            triangles[groupIndex] = uint3(primitive.i0, primitive.i1, primitive.i2);
        }
        else
        {
            triangles[groupIndex] = uint3(0u, 0u, 0u);
        }
    }
}
