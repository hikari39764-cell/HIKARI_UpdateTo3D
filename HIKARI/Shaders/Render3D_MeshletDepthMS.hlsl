#include "Include/HIKARI_MeshletMSCommon.hlsli"
#include "Include/HIKARI_GpuDrivenWaterDeform.hlsli"

struct HikariMeshletDepthVertexOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialDataIndex : TEXCOORD2;
};

HikariMeshletDepthVertexOut HikariBuildEmptyMeshletDepthVertex()
{
    HikariMeshletDepthVertexOut output = (HikariMeshletDepthVertexOut)0;
    output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    output.materialDataIndex = 0xffffffffu;
    return output;
}

HikariMeshletDepthVertexOut HikariBuildMeshletDepthVertex(
    HikariSurfaceGpuSceneInstance instance,
    HikariMeshletResolvedCluster resolved,
    ByteAddressBuffer geometry,
    uint vertexIndex)
{
    HikariMeshletDepthVertexOut output = HikariBuildEmptyMeshletDepthVertex();
    float3 localPosition =
        HikariLoadClusterVertexPosition(geometry, resolved.header, vertexIndex).xyz;
    float3 localNormal = float3(0.0f, 1.0f, 0.0f);
    HikariApplyGpuDrivenWaterDeform(
        instance,
        gTimeParams.x,
        localPosition,
        localNormal);

    float4 worldPos = mul(instance.clusterWorld, float4(localPosition, 1.0f));
    float4 uv01 = HikariLoadClusterVertexUv01(geometry, resolved.header, vertexIndex);
    output.position = mul(gViewProj, worldPos);
    output.uv = uv01.xy;
    output.uv1 = uv01.zw;
    output.materialDataIndex = instance.materialDataIndex;
    return output;
}

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void main(
    uint groupIndex : SV_GroupIndex,
    uint3 groupId : SV_GroupID,
    in payload HikariMeshletPayload payload,
    out vertices HikariMeshletDepthVertexOut vertices[HIKARI_MESHLET_MAX_VERTICES],
    out indices uint3 triangles[HIKARI_MESHLET_MAX_PRIMITIVES])
{
    HikariMeshletResolvedCluster resolved =
        HikariResolveMeshletCluster(payload, groupId);
    SetMeshOutputCounts(resolved.vertexCount, resolved.primitiveCount);
    if (!resolved.valid)
    {
        if (groupIndex < HIKARI_MESHLET_MAX_VERTICES)
        {
            vertices[groupIndex] = HikariBuildEmptyMeshletDepthVertex();
        }
        if (groupIndex < HIKARI_MESHLET_MAX_PRIMITIVES)
        {
            triangles[groupIndex] = uint3(0u, 0u, 0u);
        }
        return;
    }

    ByteAddressBuffer geometry =
        gClusterGeometryPool[NonUniformResourceIndex(resolved.clusterGeometryPoolIndex)];
    HikariSurfaceGpuSceneInstance instance =
        gSurfaceGpuSceneBuffer[resolved.visible.gpuSceneInstanceIndex];

    if (groupIndex < resolved.vertexCount)
    {
        uint vertexIndex = 0u;
        if (HikariResolveMeshletVertexIndex(resolved, groupIndex, vertexIndex))
        {
            vertices[groupIndex] =
                HikariBuildMeshletDepthVertex(instance, resolved, geometry, vertexIndex);
        }
        else
        {
            vertices[groupIndex] = HikariBuildEmptyMeshletDepthVertex();
        }
    }

    if (groupIndex < resolved.primitiveCount)
    {
        uint3 primitive =
            HikariLoadResolvedMeshletPrimitive(geometry, resolved, groupIndex);
        triangles[groupIndex] =
            primitive.x < resolved.vertexCount &&
            primitive.y < resolved.vertexCount &&
            primitive.z < resolved.vertexCount
                ? primitive
                : uint3(0u, 0u, 0u);
    }
}
