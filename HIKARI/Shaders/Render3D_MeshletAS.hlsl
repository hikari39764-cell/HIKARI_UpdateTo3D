#include "Include/HIKARI_MeshletDraw.hlsli"
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_CB 1
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_HELPERS 1
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/HIKARI_ClusterGpuData.hlsli"
#include "Include/HIKARI_RenderDescriptorLayout.hlsli"

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

cbuffer CullingCameraCB : register(b9)
{
    float4x4 gCullViewProj;
    float4x4 gCullInvViewProj;
    float4 gCullCameraPos;
    float4 gCullTimeParams;
    float4 gCullScreenParams;
};

static const uint HIKARI_CLUSTER_SRV_POOL_BEGIN = HIKARI_RENDER_SYSTEM_SRV_DYNAMIC_BEGIN;
static const uint HIKARI_CLUSTER_SRV_POOL_COUNT = HIKARI_RENDER_SYSTEM_SRV_DYNAMIC_COUNT;
static const uint HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD = 64u;
static const uint HIKARI_MESHLET_AS_MODE_COMPACT = 0u;
static const uint HIKARI_MESHLET_AS_MODE_DENSE = 1u;
static const uint HIKARI_MESHLET_AS_PASS_SHADOW = 3u;
static const float HIKARI_MESHLET_AS_CONE_NEAR_RADIUS_SCALE = 2.0f;
static const float HIKARI_MESHLET_AS_CONE_RADIUS_BIAS = 0.02f;
static const float HIKARI_MESHLET_AS_CONE_DISTANCE_BIAS = 0.001f;

ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

struct HikariMeshletPayload
{
    uint visibleRangeIndex;
    uint visibleClusterCount;
    uint clusterMode;
    uint reserved0;
    uint clusterOffsets[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
};

groupshared HikariMeshletPayload gMeshletAsPayload;

float4 HikariMeshletAsMatrixRow0(float4x4 matrix)
{
    return float4(matrix._11, matrix._12, matrix._13, matrix._14);
}

float4 HikariMeshletAsMatrixRow1(float4x4 matrix)
{
    return float4(matrix._21, matrix._22, matrix._23, matrix._24);
}

float4 HikariMeshletAsMatrixRow2(float4x4 matrix)
{
    return float4(matrix._31, matrix._32, matrix._33, matrix._34);
}

float4 HikariMeshletAsMatrixRow3(float4x4 matrix)
{
    return float4(matrix._41, matrix._42, matrix._43, matrix._44);
}

bool HikariMeshletAsPlaneVisible(float4 plane, float3 center, float radius)
{
    float planeLength = length(plane.xyz);
    if (planeLength <= 0.000001f)
    {
        return true;
    }

    float distance = dot(plane.xyz, center) + plane.w;
    return distance >= -radius * planeLength;
}

bool HikariMeshletAsSphereVisible(float4 boundsCenterRadius)
{
    float3 center = boundsCenterRadius.xyz;
    float radius = max(boundsCenterRadius.w, 0.0f);

    float4 row0 = HikariMeshletAsMatrixRow0(gCullViewProj);
    float4 row1 = HikariMeshletAsMatrixRow1(gCullViewProj);
    float4 row2 = HikariMeshletAsMatrixRow2(gCullViewProj);
    float4 row3 = HikariMeshletAsMatrixRow3(gCullViewProj);

    return
        HikariMeshletAsPlaneVisible(row3 + row0, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 - row0, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 + row1, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 - row1, center, radius) &&
        HikariMeshletAsPlaneVisible(row2, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 - row2, center, radius);
}

float4 HikariMeshletAsBuildWorldSphere(
    float4x4 world,
    float4 localSphere,
    float4 boundsMin,
    float4 boundsMax)
{
    float3 localCenter = localSphere.xyz;
    float localRadius = localSphere.w;
    if (localRadius <= 0.000001f)
    {
        localCenter = (boundsMin.xyz + boundsMax.xyz) * 0.5f;
        float3 extents = max(boundsMax.xyz - localCenter, float3(0.0f, 0.0f, 0.0f));
        localRadius = length(extents);
    }

    float3 worldCenter = mul(world, float4(localCenter, 1.0f)).xyz;
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);
    float worldScale = max(length(axisX), max(length(axisY), length(axisZ)));
    return float4(worldCenter, max(localRadius * worldScale, 0.0f));
}

float3 HikariMeshletAsTransformNormalAxis(float4x4 world, float3 localAxis)
{
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);

    float3 normalAxis =
        localAxis.x * cross(axisY, axisZ) +
        localAxis.y * cross(axisZ, axisX) +
        localAxis.z * cross(axisX, axisY);
    if (length(normalAxis) <= 0.000001f)
    {
        normalAxis = mul(world, float4(localAxis, 0.0f)).xyz;
    }
    if (length(normalAxis) <= 0.000001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return normalize(normalAxis);
}

bool HikariMeshletAsConeBackfacing(
    float4x4 world,
    HikariMeshCluster cluster,
    float4 worldSphere)
{
    float3 localAxis = cluster.coneAxisCutoff.xyz;
    float localAxisLength = length(localAxis);
    float coneCutoff = cluster.coneAxisCutoff.w;
    if (localAxisLength <= 0.000001f ||
        coneCutoff <= 0.0f ||
        coneCutoff >= 1.0f)
    {
        return false;
    }

    float3 axis = HikariMeshletAsTransformNormalAxis(world, localAxis / localAxisLength);
    if (length(axis) <= 0.000001f)
    {
        return false;
    }

    float sphereRadius = max(worldSphere.w, 0.0f);
    if (sphereRadius <= 0.000001f)
    {
        return false;
    }

    float3 view = worldSphere.xyz - gCullCameraPos.xyz;
    float viewLength = length(view);
    if (viewLength <= max(0.0001f, sphereRadius * HIKARI_MESHLET_AS_CONE_NEAR_RADIUS_SCALE))
    {
        return false;
    }

    float rejectThreshold = coneCutoff * viewLength + sphereRadius;
    float stabilityBias = max(
        viewLength * HIKARI_MESHLET_AS_CONE_DISTANCE_BIAS,
        sphereRadius * HIKARI_MESHLET_AS_CONE_RADIUS_BIAS);
    return dot(view, axis) >= rejectThreshold + stabilityBias;
}

bool HikariMeshletAsClusterVisible(
    HikariMeshletVisibleRange visible,
    HikariSurfaceGpuSceneInstance instance,
    ByteAddressBuffer geometry,
    HikariClusterGeometryHeader header,
    uint localClusterOffset)
{
    if (localClusterOffset >= visible.clusterCount)
    {
        return false;
    }

    uint clusterIndex = visible.firstCluster + localClusterOffset;
    if (clusterIndex >= header.clusterCount)
    {
        return false;
    }

    HikariMeshCluster cluster = HikariLoadMeshCluster(geometry, header, clusterIndex);
    if (cluster.indexCount == 0u)
    {
        return false;
    }

    if (visible.passKind == HIKARI_MESHLET_AS_PASS_SHADOW)
    {
        return true;
    }

    float4 worldSphere = HikariMeshletAsBuildWorldSphere(
        instance.clusterWorld,
        cluster.sphereCenterRadius,
        cluster.boundsMin,
        cluster.boundsMax);
    if (!HikariMeshletAsSphereVisible(worldSphere))
    {
        return false;
    }

    if ((visible.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED) == 0u &&
        HikariMeshletAsConeBackfacing(instance.clusterWorld, cluster, worldSphere))
    {
        return false;
    }

    return true;
}

void HikariMeshletAsInitPayload(uint visibleRangeIndex)
{
    gMeshletAsPayload.visibleRangeIndex = visibleRangeIndex;
    gMeshletAsPayload.visibleClusterCount = 0u;
    gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_COMPACT;
    gMeshletAsPayload.reserved0 = 0u;
}

[numthreads(64, 1, 1)]
void main(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    uint visibleRangeIndex = gMeshletVisibleRangeIndex + groupId.x;
    if (groupIndex == 0u)
    {
        HikariMeshletAsInitPayload(visibleRangeIndex);
    }
    GroupMemoryBarrierWithGroupSync();

    HikariMeshletVisibleRange visible = gMeshletVisibleRanges[visibleRangeIndex];
    bool rangeValid =
        visible.clusterCount != 0u &&
        visible.clusterGeometrySrvDescriptorIndex != 0xffffffffu;
    uint clusterGeometryPoolIndex =
        rangeValid
            ? visible.clusterGeometrySrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
            : 0u;
    rangeValid =
        rangeValid &&
        visible.clusterGeometrySrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN &&
        clusterGeometryPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT;

    bool useDenseRange =
        rangeValid &&
        visible.clusterCount > HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD;
    if (useDenseRange && groupIndex == 0u)
    {
        gMeshletAsPayload.visibleClusterCount = visible.clusterCount;
        gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_DENSE;
    }

    bool useCompactRange =
        rangeValid &&
        !useDenseRange;
    if (useCompactRange)
    {
        ByteAddressBuffer geometry =
            gClusterGeometryPool[NonUniformResourceIndex(clusterGeometryPoolIndex)];
        HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
        header.clusterOffsetBytes = visible.clusterOffsetBytes;
        header.vertexOffsetBytes = visible.vertexOffsetBytes;
        header.vertexCount = visible.vertexCount;
        header.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
        header.meshletPrimitiveCount = visible.meshletPrimitiveCount;
        header.clusterCount = visible.geometryClusterCount;

        HikariSurfaceGpuSceneInstance instance =
            gSurfaceGpuSceneBuffer[visible.gpuSceneInstanceIndex];

        if (groupIndex < visible.clusterCount &&
            HikariMeshletAsClusterVisible(
                visible,
                instance,
                geometry,
                header,
                groupIndex))
        {
            uint writeIndex = 0u;
            InterlockedAdd(gMeshletAsPayload.visibleClusterCount, 1u, writeIndex);
            if (writeIndex < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD)
            {
                gMeshletAsPayload.clusterOffsets[writeIndex] = groupIndex;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (useCompactRange &&
        gMeshletAsPayload.visibleClusterCount == 0u &&
        visible.clusterCount != 0u &&
        groupIndex == 0u)
    {
        gMeshletAsPayload.visibleClusterCount = visible.clusterCount;
        gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_DENSE;
    }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(
        max(gMeshletAsPayload.visibleClusterCount, 1u),
        1u,
        1u,
        gMeshletAsPayload);
}
