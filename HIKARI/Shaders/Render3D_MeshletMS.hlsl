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
static const uint HIKARI_MESHLET_MAX_PRIMITIVES = 64u;
static const uint HIKARI_MESHLET_MAX_VERTICES = HIKARI_MESHLET_MAX_PRIMITIVES * 3u;

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
    HikariClusterVertex vertex,
    uint surfaceGpuSceneIndex)
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
    HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(geometry);
    uint clusterIndex = visible.firstCluster + groupId.x;
    valid =
        valid &&
        HikariIsValidClusterGeometryHeader(header) &&
        clusterIndex < header.clusterCount;
    uint safeClusterIndex = valid ? clusterIndex : 0u;

    HikariMeshCluster cluster = HikariLoadMeshCluster(geometry, header, safeClusterIndex);
    uint primitiveCount = min(cluster.triangleCount, HIKARI_MESHLET_MAX_PRIMITIVES);
    if (!valid || cluster.firstPrimitive >= header.meshletPrimitiveCount)
    {
        primitiveCount = 0u;
    }
    else
    {
        primitiveCount = min(
            primitiveCount,
            header.meshletPrimitiveCount - cluster.firstPrimitive);
    }

    uint vertexCount = primitiveCount * 3u;
    SetMeshOutputCounts(vertexCount, primitiveCount);
    if (!valid || groupIndex >= primitiveCount)
    {
        return;
    }

    HikariSurfaceGpuSceneInstance instance =
        gSurfaceGpuSceneBuffer[visible.gpuSceneInstanceIndex];
    HikariMeshletPrimitive primitive =
        HikariLoadMeshletPrimitive(
            geometry,
            header,
            cluster.firstPrimitive + groupIndex);

    HikariClusterVertex v0;
    HikariClusterVertex v1;
    HikariClusterVertex v2;
    if (!HikariResolveMeshletVertex(geometry, header, cluster, primitive.i0, v0) ||
        !HikariResolveMeshletVertex(geometry, header, cluster, primitive.i1, v1) ||
        !HikariResolveMeshletVertex(geometry, header, cluster, primitive.i2, v2))
    {
        triangles[groupIndex] = uint3(0u, 0u, 0u);
        return;
    }

    uint outVertexBase = groupIndex * 3u;
    vertices[outVertexBase + 0u] =
        HikariBuildMeshletVertex(instance, v0, visible.gpuSceneInstanceIndex);
    vertices[outVertexBase + 1u] =
        HikariBuildMeshletVertex(instance, v1, visible.gpuSceneInstanceIndex);
    vertices[outVertexBase + 2u] =
        HikariBuildMeshletVertex(instance, v2, visible.gpuSceneInstanceIndex);
    triangles[groupIndex] = uint3(
        outVertexBase + 0u,
        outVertexBase + 1u,
        outVertexBase + 2u);
}
