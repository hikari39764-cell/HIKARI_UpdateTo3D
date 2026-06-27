#include "Include/HIKARI_MeshletMSCommon.hlsli"
#include "Include/HIKARI_GpuDrivenWaterDeform.hlsli"

struct HikariMeshletVertexOut
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
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

HikariMeshletVertexOut HikariBuildMeshletVertex(
    HikariSurfaceGpuSceneInstance instance,
    HikariMeshletResolvedCluster resolved,
    HikariClusterVertex vertex,
    uint surfaceGpuSceneIndex)
{
    HikariMeshletVertexOut output = HikariBuildEmptyMeshletVertex();
    float3 localPosition = vertex.position.xyz;
    float3 localNormal = vertex.normal.xyz;
    HikariApplyGpuDrivenWaterDeform(
        instance,
        gTimeParams.x,
        localPosition,
        localNormal);

    float4 worldPos = mul(instance.clusterWorld, float4(localPosition, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)instance.clusterNormalMatrix, localNormal));
    output.tangentWS = float4(
        normalize(mul((float3x3)instance.clusterNormalMatrix, vertex.tangent.xyz)),
        vertex.tangent.w);
    output.uv = vertex.uv01.xy;
    output.uv1 = vertex.uv01.zw;
    output.materialDataIndex = instance.materialDataIndex;
    output.receiveShadow =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW) != 0u ? 1u : 0u;
    output.objectDataIndex = surfaceGpuSceneIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.debugClusterId = resolved.clusterIndex;
    output.debugSurfaceId =
        resolved.visible.clusterSurfaceIndex * 4099u + resolved.visible.sectionIndex;
    output.debugLodIndex = resolved.visible.lodIndex;
    output.debugDrawBucket = resolved.visible.drawBucket;
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
    HikariMeshletResolvedCluster resolved =
        HikariResolveMeshletCluster(payload, groupId);
    SetMeshOutputCounts(resolved.vertexCount, resolved.primitiveCount);
    if (!resolved.valid)
    {
        if (groupIndex < HIKARI_MESHLET_MAX_VERTICES)
        {
            vertices[groupIndex] = HikariBuildEmptyMeshletVertex();
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
            HikariClusterVertex vertex =
                HikariLoadClusterVertexShading(geometry, resolved.header, vertexIndex);
            vertices[groupIndex] =
                HikariBuildMeshletVertex(
                    instance,
                    resolved,
                    vertex,
                    resolved.visible.gpuSceneInstanceIndex);
        }
        else
        {
            vertices[groupIndex] = HikariBuildEmptyMeshletVertex();
        }
    }

    if (groupIndex < resolved.primitiveCount)
    {
        uint3 primitive =
            HikariLoadResolvedMeshletPrimitive(geometry, resolved, groupIndex);
        if (primitive.x < resolved.vertexCount &&
            primitive.y < resolved.vertexCount &&
            primitive.z < resolved.vertexCount)
        {
            triangles[groupIndex] = primitive;
        }
        else
        {
            triangles[groupIndex] = uint3(0u, 0u, 0u);
        }
    }
}
