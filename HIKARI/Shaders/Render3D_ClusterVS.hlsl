#define gFxUser0 gFxUser[0]
#define gFxUser1 gFxUser[1]
#define gFxUser2 gFxUser[2]
#define gFxUser3 gFxUser[3]
#define gFxUser4 gFxUser[4]
#define gFxUser5 gFxUser[5]
#define gFxUser6 gFxUser[6]
#define gFxUser7 gFxUser[7]

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

#include "Include/HIKARI_MeshObjectData.hlsli"
#include "Include/HIKARI_ClusterGpuData.hlsli"
#include "Include/HIKARI_GpuDrivenWaterDeform.hlsli"

static const uint HIKARI_CLUSTER_SRV_POOL_BEGIN = 3985u;
static const uint HIKARI_CLUSTER_SRV_POOL_COUNT = 111u;
ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

struct VSInput
{
    uint vertexId : SV_VertexID;
    uint instanceId : SV_InstanceID;
};

struct VSOutput
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

VSOutput HikariBuildEmptyClusterVertex()
{
    VSOutput output = (VSOutput)0;
    output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    output.normalWS = float3(0.0f, 1.0f, 0.0f);
    output.tangentWS = float4(1.0f, 0.0f, 0.0f, 1.0f);
    return output;
}

VSOutput main(VSInput input)
{
    uint surfaceGpuSceneIndex = gSurfaceGpuSceneBaseIndex;
    uint firstIndex = gUseSurfaceGpuScene;
    HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
    HikariMeshObjectData objectData =
        HikariBuildMeshObjectDataFromSurfaceGpuScene(instance);

    VSOutput output = HikariBuildEmptyClusterVertex();
    output.materialDataIndex = objectData.materialDataIndex;
    output.receiveShadow = objectData.receiveShadow;
    output.objectDataIndex = surfaceGpuSceneIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.debugClusterId = 0u;
    output.debugSurfaceId = instance.clusterSurfaceIndex;
    output.debugLodIndex = instance.clusterSelectedLodIndex;
    output.debugDrawBucket = 0u;

    if (instance.clusterGeometrySrvDescriptorIndex < HIKARI_CLUSTER_SRV_POOL_BEGIN)
    {
        return output;
    }

    uint clusterGeometryPoolIndex =
        instance.clusterGeometrySrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN;
    if (clusterGeometryPoolIndex >= HIKARI_CLUSTER_SRV_POOL_COUNT)
    {
        return output;
    }

    ByteAddressBuffer geometry =
        gClusterGeometryPool[NonUniformResourceIndex(clusterGeometryPoolIndex)];
    HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(geometry);
    if (!HikariIsValidClusterGeometryHeader(header) ||
        firstIndex + input.vertexId >= header.indexCount)
    {
        return output;
    }

    uint packedIndex = HikariLoadClusterIndex(
        geometry,
        header,
        firstIndex + input.vertexId);
    if (packedIndex >= header.vertexCount)
    {
        return output;
    }

    HikariClusterVertex vertex =
        HikariLoadClusterVertex(geometry, header, packedIndex);

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
    return output;
}
