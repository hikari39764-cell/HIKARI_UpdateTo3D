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

static const uint HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED = 1u;

ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

groupshared HikariMeshletPayload gMeshletAsPayload;
groupshared uint gMeshletAsVisibleCount;

void HikariMeshletAsInitPayload(uint visibleRangeIndex)
{
    gMeshletAsPayload.visibleRangeIndex = visibleRangeIndex;
    gMeshletAsPayload.visibleClusterCount = 0u;
    gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_COMPACT;
    gMeshletAsPayload.clusterGeometryPoolIndex = 0u;
    gMeshletAsPayload.gpuSceneInstanceIndex = 0u;
    gMeshletAsPayload.clusterSurfaceIndex = 0u;
    gMeshletAsPayload.sectionIndex = 0u;
    gMeshletAsPayload.lodIndex = 0u;
    gMeshletAsPayload.drawBucket = 0u;
    gMeshletAsPayload.vertexOffsetBytes = 0u;
    gMeshletAsPayload.vertexCount = 0u;
    gMeshletAsPayload.meshletPrimitiveOffsetBytes = 0u;
    gMeshletAsPayload.meshletPrimitiveCount = 0u;
    gMeshletAsPayload.firstCluster = 0u;
    gMeshletAsPayload.rangeClusterCount = 0u;
    gMeshletAsPayload.geometryClusterCount = 0u;
    gMeshletAsVisibleCount = 0u;
}

void HikariMeshletAsStoreRangePayload(
    HikariMeshletVisibleRange visible,
    uint clusterGeometryPoolIndex)
{
    gMeshletAsPayload.clusterGeometryPoolIndex = clusterGeometryPoolIndex;
    gMeshletAsPayload.gpuSceneInstanceIndex = visible.gpuSceneInstanceIndex;
    gMeshletAsPayload.clusterSurfaceIndex = visible.clusterSurfaceIndex;
    gMeshletAsPayload.sectionIndex = visible.sectionIndex;
    gMeshletAsPayload.lodIndex = visible.lodIndex;
    gMeshletAsPayload.drawBucket = visible.drawBucket;
    gMeshletAsPayload.vertexOffsetBytes = visible.vertexOffsetBytes;
    gMeshletAsPayload.vertexCount = visible.vertexCount;
    gMeshletAsPayload.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
    gMeshletAsPayload.meshletPrimitiveCount = visible.meshletPrimitiveCount;
    gMeshletAsPayload.firstCluster = visible.firstCluster;
    gMeshletAsPayload.rangeClusterCount = visible.clusterCount;
    gMeshletAsPayload.geometryClusterCount = visible.geometryClusterCount;
}

bool HikariMeshletAsBuildPayloadCluster(
    HikariClusterGeometryHeader header,
    uint clusterIndex,
    HikariMeshCluster cluster,
    out HikariMeshletPayloadCluster payloadCluster)
{
    payloadCluster = (HikariMeshletPayloadCluster)0;
    uint vertexCount =
        min(cluster.vertexCount, HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_VERTICES);
    uint primitiveCount =
        min(cluster.triangleCount, HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_PRIMITIVES);
    bool valid =
        vertexCount != 0u &&
        primitiveCount != 0u &&
        cluster.firstVertex < header.vertexCount &&
        cluster.firstPrimitive < header.meshletPrimitiveCount;
    if (!valid)
    {
        return false;
    }

    vertexCount =
        min(
            vertexCount,
            header.vertexCount - cluster.firstVertex);
    primitiveCount =
        min(
            primitiveCount,
            header.meshletPrimitiveCount - cluster.firstPrimitive);
    valid = vertexCount != 0u && primitiveCount != 0u;
    if (!valid)
    {
        return false;
    }

    payloadCluster.clusterIndex = clusterIndex;
    payloadCluster.firstVertex = cluster.firstVertex;
    payloadCluster.firstPrimitive = cluster.firstPrimitive;
    payloadCluster.packedCounts =
        HikariMeshletPackPayloadCounts(vertexCount, primitiveCount);
    return true;
}

void HikariMeshletAsStoreVisibleCluster(
    uint compactIndex,
    HikariMeshletPayloadCluster payloadCluster)
{
    if (compactIndex < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD)
    {
        gMeshletAsPayload.clusters[compactIndex] = payloadCluster;
    }
}

bool HikariMeshletAsTryStoreVisibleCluster(
    HikariClusterGeometryHeader header,
    uint clusterIndex,
    HikariMeshCluster cluster)
{
    HikariMeshletPayloadCluster payloadCluster;
    if (!HikariMeshletAsBuildPayloadCluster(
        header,
        clusterIndex,
        cluster,
        payloadCluster))
    {
        return false;
    }

    uint compactIndex = 0u;
    InterlockedAdd(gMeshletAsVisibleCount, 1u, compactIndex);
    HikariMeshletAsStoreVisibleCluster(compactIndex, payloadCluster);
    return true;
}

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

    return dot(plane.xyz, center) + plane.w >= -radius * planeLength;
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
        localRadius = length(max(boundsMax.xyz - localCenter, float3(0.0f, 0.0f, 0.0f)));
    }

    float3 worldCenter = mul(world, float4(localCenter, 1.0f)).xyz;
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);
    float worldScale = max(length(axisX), max(length(axisY), length(axisZ)));
    return float4(worldCenter, max(localRadius * worldScale, 0.0f));
}

bool HikariMeshletAsSphereVisible(float4 worldSphere)
{
    float3 center = worldSphere.xyz;
    float radius = max(worldSphere.w, 0.0f);
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
    return length(normalAxis) > 0.000001f
        ? normalize(normalAxis)
        : float3(0.0f, 0.0f, 1.0f);
}

bool HikariMeshletAsConeBackfacing(
    float4x4 world,
    HikariMeshCluster cluster,
    float4 worldSphere)
{
    float cutoff = cluster.coneAxisCutoff.w;
    if (cutoff <= 0.0f || cutoff >= 1.0f)
    {
        return false;
    }

    float3 axis = HikariMeshletAsTransformNormalAxis(world, cluster.coneAxisCutoff.xyz);
    float3 view = normalize(gCullCameraPos.xyz - worldSphere.xyz);
    float radius = max(worldSphere.w, 0.0f);
    float distanceToCamera = length(gCullCameraPos.xyz - worldSphere.xyz);
    float radiusBias = radius / max(distanceToCamera, 0.0001f);
    return dot(axis, view) <= -cutoff - radiusBias - 0.001f;
}

[numthreads(HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD, 1, 1)]
void main(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    uint visibleRangeIndex = gMeshletVisibleRangeIndex + groupId.x;

    if (groupIndex == 0u)
    {
        HikariMeshletAsInitPayload(visibleRangeIndex);
    }
    GroupMemoryBarrierWithGroupSync();

    HikariMeshletVisibleRange visible = gMeshletVisibleRanges[visibleRangeIndex];
    bool clusterListRange = HikariMeshletVisibleRangeUsesClusterList(visible);
    bool packetRange = HikariMeshletVisibleRangeUsesPacket(visible);
    bool preculledRange = HikariMeshletVisibleRangeIsPreculled(visible);
    uint clusterListCount = HikariMeshletVisibleRangeClusterListCount(visible);
    uint packetClusterCount = HikariMeshletVisibleRangePacketCount(visible);

    bool rangeValid =
        (clusterListRange
            ? clusterListCount
            : (packetRange ? packetClusterCount : visible.clusterCount)) != 0u &&
        visible.clusterGeometryMetadataSrvDescriptorIndex != 0xffffffffu &&
        visible.clusterGeometryMetadataSrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN;

    uint clusterMetadataPoolIndex =
        rangeValid
            ? visible.clusterGeometryMetadataSrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
            : 0u;
    uint clusterGeometryPoolIndex =
        rangeValid && visible.clusterGeometrySrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN
            ? visible.clusterGeometrySrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
            : 0u;

    rangeValid =
        rangeValid &&
        visible.clusterGeometrySrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN &&
        clusterMetadataPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT &&
        clusterGeometryPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT;

    if (rangeValid && groupIndex == 0u)
    {
        HikariMeshletAsStoreRangePayload(visible, clusterGeometryPoolIndex);
    }

    bool useDenseRange =
        rangeValid &&
        !clusterListRange &&
        !packetRange &&
        preculledRange &&
        visible.clusterCount > HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD;
    bool directCompactedRange =
        rangeValid &&
        !useDenseRange &&
        (clusterListRange || packetRange || preculledRange);
    if (useDenseRange && groupIndex == 0u)
    {
        gMeshletAsPayload.visibleClusterCount = visible.clusterCount;
        gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_DENSE;
    }

    if (directCompactedRange)
    {
        uint compactCount = visible.clusterCount;
        if (clusterListRange)
        {
            compactCount = clusterListCount;
        }
        else if (packetRange)
        {
            compactCount = packetClusterCount;
        }
        compactCount = min(compactCount, HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD);

        ByteAddressBuffer metadata =
            gClusterGeometryPool[NonUniformResourceIndex(clusterMetadataPoolIndex)];
        HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
        header.clusterOffsetBytes = visible.clusterOffsetBytes;
        header.vertexOffsetBytes = visible.vertexOffsetBytes;
        header.vertexCount = visible.vertexCount;
        header.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
        header.meshletPrimitiveCount = visible.meshletPrimitiveCount;
        header.clusterCount = visible.geometryClusterCount;

        uint clusterIndex = 0xffffffffu;
        bool clusterValid =
            groupIndex < compactCount;
        if (clusterValid && clusterListRange)
        {
            uint listStart = HikariMeshletVisibleRangeClusterListStart(visible);
            clusterValid = listStart != 0xffffffffu;
            clusterIndex = clusterValid
                ? gMeshletVisibleClusterList[listStart + groupIndex]
                : 0xffffffffu;
        }
        else if (clusterValid && packetRange)
        {
            clusterIndex = HikariMeshletVisibleRangePacketIndex(visible, groupIndex);
        }
        else if (clusterValid)
        {
            clusterIndex = visible.firstCluster + groupIndex;
        }
        clusterValid =
            clusterValid &&
            clusterIndex != 0xffffffffu &&
            clusterIndex < header.clusterCount;

        HikariSurfaceGpuSceneInstance instance =
            gSurfaceGpuSceneBuffer[visible.gpuSceneInstanceIndex];
        HikariMeshCluster cluster =
            HikariLoadMeshCluster(metadata, header, clusterValid ? clusterIndex : 0u);

        clusterValid =
            clusterValid &&
            cluster.indexCount != 0u &&
            cluster.surfaceIndex == visible.clusterSurfaceIndex;
        if (clusterValid && !preculledRange)
        {
            HikariSurfaceGpuSceneInstance instance =
                gSurfaceGpuSceneBuffer[visible.gpuSceneInstanceIndex];
            float4 worldSphere = HikariMeshletAsBuildWorldSphere(
                instance.clusterWorld,
                cluster.sphereCenterRadius,
                cluster.boundsMin,
                cluster.boundsMax);
            clusterValid = HikariMeshletAsSphereVisible(worldSphere);

            bool canConeCull =
                visible.drawBucket != HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED &&
                (visible.flags & (
                    HIKARI_SURFACE_GPU_SCENE_FLAG_ALPHA_MASKED |
                    HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT |
                    HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED)) == 0u;
            if (clusterValid && canConeCull &&
                HikariMeshletAsConeBackfacing(instance.clusterWorld, cluster, worldSphere))
            {
                clusterValid = false;
            }
        }

        if (clusterValid)
        {
            HikariMeshletAsTryStoreVisibleCluster(header, clusterIndex, cluster);
        }
    }
    else if (rangeValid && !useDenseRange)
    {
        ByteAddressBuffer metadata =
            gClusterGeometryPool[NonUniformResourceIndex(clusterMetadataPoolIndex)];
        HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
        header.clusterOffsetBytes = visible.clusterOffsetBytes;
        header.vertexOffsetBytes = visible.vertexOffsetBytes;
        header.vertexCount = visible.vertexCount;
        header.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
        header.meshletPrimitiveCount = visible.meshletPrimitiveCount;
        header.clusterCount = visible.geometryClusterCount;

        uint localClusterOffset = groupIndex;
        uint clusterIndex = visible.firstCluster + localClusterOffset;
        bool clusterValid =
            localClusterOffset < min(visible.clusterCount, HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD) &&
            clusterIndex < header.clusterCount;

        HikariSurfaceGpuSceneInstance instance =
            gSurfaceGpuSceneBuffer[visible.gpuSceneInstanceIndex];
        HikariMeshCluster cluster =
            HikariLoadMeshCluster(metadata, header, clusterValid ? clusterIndex : 0u);

        float4 worldSphere = HikariMeshletAsBuildWorldSphere(
            instance.clusterWorld,
            cluster.sphereCenterRadius,
            cluster.boundsMin,
            cluster.boundsMax);
        clusterValid =
            clusterValid &&
            cluster.indexCount != 0u &&
            cluster.surfaceIndex == visible.clusterSurfaceIndex &&
            HikariMeshletAsSphereVisible(worldSphere);

        bool canConeCull =
            visible.drawBucket != HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED &&
            (visible.flags & (
                HIKARI_SURFACE_GPU_SCENE_FLAG_ALPHA_MASKED |
                HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT |
                HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED)) == 0u;
        if (clusterValid && canConeCull &&
            HikariMeshletAsConeBackfacing(instance.clusterWorld, cluster, worldSphere))
        {
            clusterValid = false;
        }

        if (clusterValid)
        {
            HikariMeshletAsTryStoreVisibleCluster(header, clusterIndex, cluster);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0u)
    {
        if (!useDenseRange)
        {
            gMeshletAsPayload.visibleClusterCount =
                min(gMeshletAsVisibleCount, HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD);
            gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_COMPACT;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 全滅時は MS group を発行しない (DispatchMesh(0) は仕様上合法)。
    DispatchMesh(
        gMeshletAsPayload.visibleClusterCount,
        1u,
        1u,
        gMeshletAsPayload);
}
