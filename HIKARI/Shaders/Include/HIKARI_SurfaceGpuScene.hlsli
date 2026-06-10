#ifndef HIKARI_SURFACE_GPU_SCENE_INCLUDED
#define HIKARI_SURFACE_GPU_SCENE_INCLUDED

// C++ 側の SurfaceGpuSceneInstance と同じ layout に保つ。
struct HikariSurfaceGpuSceneInstance
{
    float4x4 world;
    float4x4 normalMatrix;
    float4 boundsCenterRadius;

    uint sourcePacketIndex;
    uint sourceSurfaceInstanceIndex;
    uint objectIdLow;
    uint objectIdHigh;

    uint meshIndex;
    uint primitiveIndex;
    uint sourceMaterialIndex;
    uint nodeIndex;

    uint flags;
    uint materialDataIndex;
    uint clusterRangeIndex;
    uint clusterRangeCount;

    uint meshResourceIndex;
    uint meshResourceGeneration;
    uint materialResourceIndex;
    uint materialResourceGeneration;

    uint clusterGeometryResourceIndex;
    uint clusterGeometryResourceGeneration;
    uint resourceFlags;
    uint fxFlags;

    float4 fxUser[8];
};

static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW = 1u << 2;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_MESH = 1u << 0;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_MATERIAL = 1u << 1;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY = 1u << 2;

StructuredBuffer<HikariSurfaceGpuSceneInstance> gSurfaceGpuSceneBuffer : register(t17);

cbuffer SurfaceGpuSceneControlCB : register(b8)
{
    uint gSurfaceGpuSceneBaseIndex;
    uint gUseSurfaceGpuScene;
    uint2 gSurfaceGpuScenePadding;
};

uint HikariGetSurfaceGpuSceneAbsoluteIndex(uint instanceId)
{
    return gSurfaceGpuSceneBaseIndex + instanceId;
}

HikariSurfaceGpuSceneInstance HikariGetSurfaceGpuSceneInstanceAt(uint absoluteIndex)
{
    return gSurfaceGpuSceneBuffer[absoluteIndex];
}

HikariSurfaceGpuSceneInstance HikariGetSurfaceGpuSceneInstance(uint instanceId)
{
    return HikariGetSurfaceGpuSceneInstanceAt(HikariGetSurfaceGpuSceneAbsoluteIndex(instanceId));
}

#endif
