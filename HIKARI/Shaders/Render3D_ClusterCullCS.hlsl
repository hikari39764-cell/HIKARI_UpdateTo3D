struct ClusterCullInput
{
    float4x4 clusterWorld;
    float4 boundsCenterRadius;
    uint gpuSceneInstanceIndex;
    uint clusterGeometrySrvDescriptorIndex;
    uint firstCluster;
    uint clusterCount;
    uint clusterSurfaceIndex;
    uint passKind;
    uint flags;
    uint clusterIndexCount;
    uint firstPage;
    uint pageCount;
    uint pageTaskBaseIndex;
    uint lodIndex;
};

struct ClusterCullVisibleRange
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
    uint reserved0;
};

struct ClusterCullIndirectDrawArgument
{
    uint4 rootConstants;
    uint vertexCountPerInstance;
    uint instanceCount;
    uint startVertexLocation;
    uint startInstanceLocation;
};

struct ClusterCullMeshletDispatchArgument
{
    uint4 rootConstants;
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
    uint reserved0;
};

struct ClusterCullPageTask
{
    float4x4 clusterWorld;
    float4 boundsCenterRadius;
    uint gpuSceneInstanceIndex;
    uint clusterGeometrySrvDescriptorIndex;
    uint firstCluster;
    uint endCluster;
    uint clusterSurfaceIndex;
    uint passKind;
    uint flags;
    uint pageIndex;
    uint lodIndex;
    uint reserved1;
    uint reserved2;
    uint reserved3;
};

cbuffer ClusterCullFrameCB : register(b0)
{
    float4x4 gClusterCullViewProj;
    float4 gClusterCullCameraPosition;
    uint gClusterCullInputCount;
    uint gClusterCullVisibleRangeCapacity;
    uint gClusterCullDrawArgumentCapacity;
    uint gClusterCullEnableFrustumCull;
    uint gClusterCullDrawArgumentBucketCapacity;
    uint gClusterCullClusterSrvPoolBegin;
    uint gClusterCullClusterSrvPoolCount;
    uint gClusterCullEnableConeCull;
    uint gClusterCullEnableDebugCounters;
    uint gClusterCullSurfaceGpuSceneBaseIndex;
    uint gClusterCullPassKind;
    uint gClusterCullPageTaskCapacity;
    uint gClusterCullMergeGapIndexLimit;
    uint gClusterCullMergeRunGapIndexBudget;
    uint gClusterCullMergeMaxIndexSpan;
    uint gClusterCullMergeClusterGapLimit;
    float gClusterCullLodTargetErrorNdc;
    uint gClusterCullEnableLodErrorSelection;
    uint gClusterCullReserved0;
    uint gClusterCullReserved1;
};

RWStructuredBuffer<ClusterCullVisibleRange> gClusterCullVisibleRanges : register(u0);
RWByteAddressBuffer gClusterCullCounters : register(u1);
RWStructuredBuffer<ClusterCullIndirectDrawArgument> gClusterCullDrawArguments : register(u2);
RWStructuredBuffer<ClusterCullPageTask> gClusterCullPageTasks : register(u3);
RWStructuredBuffer<uint3> gClusterCullDispatchArguments : register(u4);
RWStructuredBuffer<ClusterCullMeshletDispatchArgument> gClusterCullMeshletDispatchArguments : register(u5);

#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/HIKARI_ClusterGpuData.hlsli"

static const uint HIKARI_CLUSTER_DRAW_BUCKET_BACK_FACE = 0u;
static const uint HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED = 1u;

static const uint HIKARI_CLUSTER_CULL_COUNTER_INPUT_COUNT = 0u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_RANGE_COUNT = 4u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_CLUSTER_COUNT = 8u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_OVERFLOW_COUNT = 12u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_COUNT = 16u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_DOUBLE_SIDED_DRAW_COUNT = 20u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_OVERFLOW_COUNT = 24u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_DOUBLE_SIDED_DRAW_OVERFLOW_COUNT = 28u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_INPUT_FRUSTUM_CULLED_COUNT = 32u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_TESTED_COUNT = 36u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_FRUSTUM_CULLED_COUNT = 40u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_TESTED_COUNT = 44u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_FRUSTUM_CULLED_COUNT = 48u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_CONE_CULLED_COUNT = 52u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_CONE_TESTED_COUNT = 56u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_DOUBLE_SIDED_CLUSTER_COUNT = 60u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT = 64u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT = 68u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_MERGED_GAP_COUNT = 72u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_MERGED_GAP_INDEX_COUNT = 76u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD0_SELECTED_COUNT = 80u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD1_SELECTED_COUNT = 84u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD2_SELECTED_COUNT = 88u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD3_PLUS_SELECTED_COUNT = 92u;

static const uint HIKARI_CLUSTER_CULL_DEFAULT_MERGE_GAP_INDEX_LIMIT = 384u;
static const uint HIKARI_CLUSTER_CULL_DEFAULT_MERGE_RUN_GAP_BUDGET = 2048u;
static const uint HIKARI_CLUSTER_CULL_DEFAULT_MERGE_MAX_INDEX_SPAN = 8192u;
// クラスタ間の穴埋めは描画量を増やしやすいため、既定では無効にする。
static const uint HIKARI_CLUSTER_CULL_DEFAULT_MERGE_CLUSTER_GAP_LIMIT = 0u;

ByteAddressBuffer gClusterGeometryPool[111] : register(t0, space1);

float4 HikariClusterCullViewProjRow0()
{
    return float4(
        gClusterCullViewProj._11,
        gClusterCullViewProj._12,
        gClusterCullViewProj._13,
        gClusterCullViewProj._14);
}

float4 HikariClusterCullViewProjRow1()
{
    return float4(
        gClusterCullViewProj._21,
        gClusterCullViewProj._22,
        gClusterCullViewProj._23,
        gClusterCullViewProj._24);
}

float4 HikariClusterCullViewProjRow2()
{
    return float4(
        gClusterCullViewProj._31,
        gClusterCullViewProj._32,
        gClusterCullViewProj._33,
        gClusterCullViewProj._34);
}

float4 HikariClusterCullViewProjRow3()
{
    return float4(
        gClusterCullViewProj._41,
        gClusterCullViewProj._42,
        gClusterCullViewProj._43,
        gClusterCullViewProj._44);
}

bool HikariClusterCullPlaneVisible(float4 plane, float3 center, float radius)
{
    float planeLength = length(plane.xyz);
    if (planeLength <= 0.000001f)
    {
        return true;
    }

    float distance = dot(plane.xyz, center) + plane.w;
    return distance >= -radius * planeLength;
}

bool HikariClusterCullSphereVisible(float4 boundsCenterRadius)
{
    if (gClusterCullEnableFrustumCull == 0)
    {
        return true;
    }

    float3 center = boundsCenterRadius.xyz;
    float radius = max(boundsCenterRadius.w, 0.0f);

    // Surface bounds are still the draw seed here, so use clip-space plane tests
    // instead of an NDC radius approximation. The latter over-culls at the
    // screen edge and can drop a whole surface while part of it is still visible.
    float4 row0 = HikariClusterCullViewProjRow0();
    float4 row1 = HikariClusterCullViewProjRow1();
    float4 row2 = HikariClusterCullViewProjRow2();
    float4 row3 = HikariClusterCullViewProjRow3();

    return
        HikariClusterCullPlaneVisible(row3 + row0, center, radius) &&
        HikariClusterCullPlaneVisible(row3 - row0, center, radius) &&
        HikariClusterCullPlaneVisible(row3 + row1, center, radius) &&
        HikariClusterCullPlaneVisible(row3 - row1, center, radius) &&
        HikariClusterCullPlaneVisible(row2, center, radius) &&
        HikariClusterCullPlaneVisible(row3 - row2, center, radius);
}

float HikariClusterCullProjectedWorldLength(float3 worldCenter, float worldLength)
{
    float4 clipCenter =
        mul(gClusterCullViewProj, float4(worldCenter, 1.0f));
    float projectionScale =
        max(length(HikariClusterCullViewProjRow0().xyz), length(HikariClusterCullViewProjRow1().xyz));
    return max(worldLength, 0.0f) *
        projectionScale /
        max(abs(clipCenter.w), 0.0001f);
}

float HikariClusterCullProjectedScreenRadius(float4 boundsCenterRadius)
{
    return HikariClusterCullProjectedWorldLength(
        boundsCenterRadius.xyz,
        boundsCenterRadius.w);
}

float HikariClusterCullProjectedLodError(
    float4 boundsCenterRadius,
    HikariClusterGeometrySurfaceLodRange range)
{
    return HikariClusterCullProjectedWorldLength(
        boundsCenterRadius.xyz,
        range.geometricError);
}

float4 HikariClusterCullBuildWorldSphere(
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

uint HikariClusterCullResolveBucket(uint flags)
{
    return (flags & HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED) != 0u
        ? HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED
        : HIKARI_CLUSTER_DRAW_BUCKET_BACK_FACE;
}

bool HikariClusterCullIsDoubleSided(uint flags)
{
    return (flags & HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED) != 0u;
}

uint HikariClusterCullMergeGapIndexLimit()
{
    return gClusterCullMergeGapIndexLimit != 0u
        ? gClusterCullMergeGapIndexLimit
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_GAP_INDEX_LIMIT;
}

uint HikariClusterCullMergeRunGapBudget()
{
    return gClusterCullMergeRunGapIndexBudget != 0u
        ? gClusterCullMergeRunGapIndexBudget
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_RUN_GAP_BUDGET;
}

uint HikariClusterCullMergeMaxIndexSpan()
{
    return gClusterCullMergeMaxIndexSpan != 0u
        ? gClusterCullMergeMaxIndexSpan
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_MAX_INDEX_SPAN;
}

uint HikariClusterCullMergeClusterGapLimit()
{
    return gClusterCullMergeClusterGapLimit != 0u
        ? gClusterCullMergeClusterGapLimit
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_CLUSTER_GAP_LIMIT;
}

float3 HikariClusterCullTransformNormalAxis(float4x4 world, float3 localAxis)
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

bool HikariClusterCullConeBackfacing(
    float4x4 world,
    HikariMeshCluster cluster,
    float4 worldSphere)
{
    float3 localAxis = cluster.coneAxisCutoff.xyz;
    float localAxisLength = length(localAxis);
    float coneCos = cluster.coneAxisCutoff.w;
    if (localAxisLength <= 0.000001f ||
        coneCos <= 0.0f ||
        coneCos > 1.0f)
    {
        return false;
    }

    float3 axis = HikariClusterCullTransformNormalAxis(world, localAxis / localAxisLength);
    if (length(axis) <= 0.000001f)
    {
        return false;
    }

    float3 toCamera = gClusterCullCameraPosition.xyz - worldSphere.xyz;
    float distanceToCamera = length(toCamera);
    float radius = max(worldSphere.w, 0.0f);
    if (distanceToCamera <= radius + 0.0001f)
    {
        return false;
    }

    float3 viewDir = toCamera / distanceToCamera;
    float sinNormalCone = sqrt(saturate(1.0f - coneCos * coneCos));
    float sinViewCone = saturate(radius / max(distanceToCamera, 0.0001f));
    float cosViewCone = sqrt(saturate(1.0f - sinViewCone * sinViewCone));
    float conservativeCutoff =
        -(sinNormalCone * cosViewCone + coneCos * sinViewCone);

    return dot(axis, viewDir) <= conservativeCutoff - 0.001f;
}

void HikariClusterCullEmitDraw(
    ClusterCullInput input,
    uint firstCluster,
    uint clusterCount,
    uint visibleClusterCount,
    uint firstIndex,
    uint indexCount,
    uint mergedGapCount,
    uint mergedGapIndexCount)
{
    if (mergedGapCount != 0u)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_MERGED_GAP_COUNT,
            mergedGapCount);
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_MERGED_GAP_INDEX_COUNT,
            mergedGapIndexCount);
    }

    uint bucket = HikariClusterCullResolveBucket(input.flags);
    uint drawCounterOffset = HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_COUNT + bucket * 4u;
    uint overflowCounterOffset =
        HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_OVERFLOW_COUNT + bucket * 4u;
    uint drawIndex = 0;
    gClusterCullCounters.InterlockedAdd(drawCounterOffset, 1, drawIndex);
    if (drawIndex >= gClusterCullDrawArgumentBucketCapacity)
    {
        gClusterCullCounters.InterlockedAdd(overflowCounterOffset, 1);
        return;
    }

    uint globalDrawIndex = bucket * gClusterCullDrawArgumentBucketCapacity + drawIndex;
    uint visibleIndex = globalDrawIndex;
    if (globalDrawIndex >= gClusterCullDrawArgumentCapacity ||
        visibleIndex >= gClusterCullVisibleRangeCapacity)
    {
        gClusterCullCounters.InterlockedAdd(overflowCounterOffset, 1);
        return;
    }

    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_RANGE_COUNT,
        1);
    if (gClusterCullEnableDebugCounters != 0u)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_CLUSTER_COUNT,
            visibleClusterCount);
    }

    ClusterCullVisibleRange visible;
    visible.gpuSceneInstanceIndex = input.gpuSceneInstanceIndex;
    visible.clusterGeometrySrvDescriptorIndex = input.clusterGeometrySrvDescriptorIndex;
    visible.firstCluster = firstCluster;
    visible.clusterCount = clusterCount;
    visible.clusterSurfaceIndex = input.clusterSurfaceIndex;
    visible.passKind = input.passKind;
    visible.flags = input.flags;
    visible.clusterIndex = firstCluster;
    visible.lodIndex = input.lodIndex;
    visible.pageIndex = input.firstPage;
    visible.drawBucket = bucket;
    visible.reserved0 = 0u;
    gClusterCullVisibleRanges[visibleIndex] = visible;

    ClusterCullIndirectDrawArgument drawArgument;
    drawArgument.rootConstants = uint4(
        input.gpuSceneInstanceIndex,
        firstIndex,
        visibleIndex,
        input.passKind);
    drawArgument.vertexCountPerInstance = indexCount;
    drawArgument.instanceCount = 1;
    drawArgument.startVertexLocation = 0;
    drawArgument.startInstanceLocation = 0;
    gClusterCullDrawArguments[globalDrawIndex] = drawArgument;
}

void HikariClusterCullFlushVisibleRun(
    ClusterCullInput input,
    inout bool hasRun,
    inout uint runFirstCluster,
    inout uint runClusterCount,
    inout uint runVisibleClusterCount,
    inout uint runFirstIndex,
    inout uint runIndexCount,
    inout uint runMergedGapCount,
    inout uint runMergedGapIndexCount)
{
    if (!hasRun)
    {
        return;
    }

    HikariClusterCullEmitDraw(
        input,
        runFirstCluster,
        runClusterCount,
        runVisibleClusterCount,
        runFirstIndex,
        runIndexCount,
        runMergedGapCount,
        runMergedGapIndexCount);

    hasRun = false;
    runFirstCluster = 0u;
    runClusterCount = 0u;
    runVisibleClusterCount = 0u;
    runFirstIndex = 0u;
    runIndexCount = 0u;
    runMergedGapCount = 0u;
    runMergedGapIndexCount = 0u;
}

void HikariClusterCullAppendVisibleCluster(
    ClusterCullInput input,
    uint clusterIndex,
    HikariMeshCluster cluster,
    inout bool hasRun,
    inout uint runFirstCluster,
    inout uint runClusterCount,
    inout uint runVisibleClusterCount,
    inout uint runFirstIndex,
    inout uint runIndexCount,
    inout uint runMergedGapCount,
    inout uint runMergedGapIndexCount)
{
    if (!hasRun)
    {
        hasRun = true;
        runFirstCluster = clusterIndex;
        runClusterCount = 1u;
        runVisibleClusterCount = 1u;
        runFirstIndex = cluster.firstIndex;
        runIndexCount = cluster.indexCount;
        runMergedGapCount = 0u;
        runMergedGapIndexCount = 0u;
        return;
    }

    uint runEndCluster = runFirstCluster + runClusterCount;
    uint runEndIndex = runFirstIndex + runIndexCount;
    bool clusterForward = clusterIndex >= runEndCluster;
    uint clusterGap = clusterForward ? clusterIndex - runEndCluster : 0xffffffffu;
    bool indexForward = cluster.firstIndex >= runEndIndex;
    uint indexGap = indexForward ? cluster.firstIndex - runEndIndex : 0xffffffffu;
    uint clusterEndIndex = cluster.firstIndex + cluster.indexCount;
    uint mergedIndexCount = clusterEndIndex >= runFirstIndex
        ? clusterEndIndex - runFirstIndex
        : 0xffffffffu;
    uint mergedClusterCount = clusterIndex >= runFirstCluster
        ? clusterIndex - runFirstCluster + 1u
        : 0xffffffffu;

    // 同一 surface/page 内の小さな欠けだけを吸収し、細かすぎる draw args を GPU 側で圧縮する。
    bool canMerge =
        clusterForward &&
        indexForward &&
        clusterGap <= HikariClusterCullMergeClusterGapLimit() &&
        indexGap <= HikariClusterCullMergeGapIndexLimit() &&
        runMergedGapIndexCount + indexGap <= HikariClusterCullMergeRunGapBudget() &&
        mergedIndexCount <= HikariClusterCullMergeMaxIndexSpan();
    if (!canMerge)
    {
        HikariClusterCullFlushVisibleRun(
            input,
            hasRun,
            runFirstCluster,
            runClusterCount,
            runVisibleClusterCount,
            runFirstIndex,
            runIndexCount,
            runMergedGapCount,
            runMergedGapIndexCount);
        hasRun = true;
        runFirstCluster = clusterIndex;
        runClusterCount = 1u;
        runVisibleClusterCount = 1u;
        runFirstIndex = cluster.firstIndex;
        runIndexCount = cluster.indexCount;
        runMergedGapCount = 0u;
        runMergedGapIndexCount = 0u;
        return;
    }

    runClusterCount = mergedClusterCount;
    ++runVisibleClusterCount;
    runIndexCount = mergedIndexCount;
    if (clusterGap != 0u || indexGap != 0u)
    {
        ++runMergedGapCount;
        runMergedGapIndexCount += indexGap;
    }
}

bool HikariClusterCullIsGpuSceneCandidate(HikariSurfaceGpuSceneInstance instance)
{
    static const uint requiredResources =
        HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY |
        HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SHADER_VISIBLE |
        HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SURFACE_RANGE;

    return
        instance.geometryBackend == HIKARI_SURFACE_GEOMETRY_BACKEND_CLUSTER_GEOMETRY &&
        (instance.resourceFlags & requiredResources) == requiredResources &&
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_CLUSTER_MAINLINE) != 0u &&
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT) == 0u &&
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX) == 0u &&
        instance.fxFlags == 0u &&
        instance.materialDataIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterGeometrySrvDescriptorIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterRangeIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterRangeCount > 0u &&
        instance.clusterSurfaceIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterIndexCount > 0u;
}

bool HikariClusterCullLodRangeUsable(
    HikariClusterGeometryHeader header,
    HikariClusterGeometrySurfaceLodRange range,
    uint surfaceIndex)
{
    return
        range.surfaceIndex == surfaceIndex &&
        range.clusterCount > 0u &&
        range.indexCount > 0u &&
        range.pageCount > 0u &&
        range.firstCluster < header.clusterCount &&
        range.firstPage < header.pageCount &&
        range.firstCluster + range.clusterCount <= header.clusterCount &&
        range.firstPage + range.pageCount <= header.pageCount;
}

bool HikariClusterCullSelectSurfaceLodRange(
    ByteAddressBuffer geometry,
    HikariClusterGeometryHeader header,
    HikariSurfaceGpuSceneInstance instance,
    out HikariClusterGeometrySurfaceLodRange selectedRange)
{
    selectedRange = (HikariClusterGeometrySurfaceLodRange)0;
    if ((instance.resourceFlags & HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_LOD_RANGES) == 0u ||
        instance.clusterLodRangeCount == 0u ||
        instance.clusterLodRangeIndex >= header.surfaceLodRangeCount)
    {
        return false;
    }

    uint rangeBegin = instance.clusterLodRangeIndex;
    uint rangeEnd = min(rangeBegin + instance.clusterLodRangeCount, header.surfaceLodRangeCount);
    float screenRadius = HikariClusterCullProjectedScreenRadius(instance.boundsCenterRadius);
    float targetProjectedError = max(gClusterCullLodTargetErrorNdc, 0.0f);
    bool useProjectedError =
        gClusterCullEnableLodErrorSelection != 0u &&
        targetProjectedError > 0.0f;
    bool hasFallback = false;

    for (uint rangeIndex = rangeBegin; rangeIndex < rangeEnd; ++rangeIndex)
    {
        HikariClusterGeometrySurfaceLodRange range =
            HikariLoadClusterGeometrySurfaceLodRange(geometry, header, rangeIndex);
        if (!HikariClusterCullLodRangeUsable(header, range, instance.clusterSurfaceIndex))
        {
            continue;
        }

        if (!hasFallback)
        {
            selectedRange = range;
            hasFallback = true;
            continue;
        }

        // 大きな surface は包囲半径だけだと高 LOD に固定されるため、投影誤差でも降段を許可する。
        bool radiusAllowsStepDown =
            screenRadius < max(selectedRange.minScreenRadius, 0.0f);
        bool errorAllowsStepDown =
            useProjectedError &&
            HikariClusterCullProjectedLodError(instance.boundsCenterRadius, range) <=
                targetProjectedError;
        if (!radiusAllowsStepDown && !errorAllowsStepDown)
        {
            break;
        }

        selectedRange = range;
    }

    return hasFallback;
}

void HikariClusterCullRecordSelectedLod(uint lodIndex)
{
    uint counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD3_PLUS_SELECTED_COUNT;
    if (lodIndex == 0u)
    {
        counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD0_SELECTED_COUNT;
    }
    else if (lodIndex == 1u)
    {
        counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD1_SELECTED_COUNT;
    }
    else if (lodIndex == 2u)
    {
        counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD2_SELECTED_COUNT;
    }
    gClusterCullCounters.InterlockedAdd(counterOffset, 1);
}

ClusterCullInput HikariClusterCullBuildInput(
    uint surfaceGpuSceneIndex,
    HikariSurfaceGpuSceneInstance instance,
    HikariClusterGeometrySurfaceLodRange lodRange)
{
    ClusterCullInput input = (ClusterCullInput)0;
    input.clusterWorld = instance.clusterWorld;
    input.boundsCenterRadius = instance.boundsCenterRadius;
    input.gpuSceneInstanceIndex = surfaceGpuSceneIndex;
    input.clusterGeometrySrvDescriptorIndex = instance.clusterGeometrySrvDescriptorIndex;
    input.firstCluster = lodRange.firstCluster;
    input.clusterCount = lodRange.clusterCount;
    input.clusterSurfaceIndex = instance.clusterSurfaceIndex;
    input.passKind = gClusterCullPassKind;
    input.flags = instance.flags;
    input.clusterIndexCount = lodRange.indexCount;
    input.firstPage = lodRange.firstPage;
    input.pageCount = lodRange.pageCount;
    input.pageTaskBaseIndex = 0u;
    input.lodIndex = lodRange.lodIndex;
    return input;
}

void HikariClusterCullProcessPage(
    ClusterCullInput input,
    ByteAddressBuffer geometry,
    HikariClusterGeometryHeader header,
    uint firstCluster,
    uint endCluster,
    uint pageIndex)
{
    bool doubleSided = HikariClusterCullIsDoubleSided(input.flags);
    bool hasRun = false;
    uint runFirstCluster = 0u;
    uint runClusterCount = 0u;
    uint runVisibleClusterCount = 0u;
    uint runFirstIndex = 0u;
    uint runIndexCount = 0u;
    uint runMergedGapCount = 0u;
    uint runMergedGapIndexCount = 0u;

    if (pageIndex >= header.pageCount)
    {
        return;
    }

    HikariClusterPage page = HikariLoadClusterPage(geometry, header, pageIndex);
    uint pageEndCluster = page.firstCluster + page.clusterCount;
    uint pageRangeStart = max(page.firstCluster, firstCluster);
    uint pageRangeEnd = min(pageEndCluster, endCluster);
    if (page.indexCount == 0 ||
        page.clusterCount == 0 ||
        pageRangeStart >= pageRangeEnd)
    {
        return;
    }

    if (gClusterCullEnableDebugCounters != 0u)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TESTED_COUNT,
            1);
    }

    float4 worldSphere = HikariClusterCullBuildWorldSphere(
        input.clusterWorld,
        float4(0.0f, 0.0f, 0.0f, 0.0f),
        page.boundsMin,
        page.boundsMax);
    if (!HikariClusterCullSphereVisible(worldSphere))
    {
        if (gClusterCullEnableDebugCounters != 0u)
        {
            gClusterCullCounters.InterlockedAdd(
                HIKARI_CLUSTER_CULL_COUNTER_PAGE_FRUSTUM_CULLED_COUNT,
                1);
        }
        HikariClusterCullFlushVisibleRun(
            input,
            hasRun,
            runFirstCluster,
            runClusterCount,
            runVisibleClusterCount,
            runFirstIndex,
            runIndexCount,
            runMergedGapCount,
            runMergedGapIndexCount);
        return;
    }

    for (uint clusterIndex = pageRangeStart; clusterIndex < pageRangeEnd; ++clusterIndex)
    {
        HikariMeshCluster cluster = HikariLoadMeshCluster(geometry, header, clusterIndex);
        if (cluster.indexCount == 0 ||
            cluster.surfaceIndex != input.clusterSurfaceIndex ||
            cluster.firstIndex >= header.indexCount ||
            cluster.firstIndex + cluster.indexCount > header.indexCount)
        {
            HikariClusterCullFlushVisibleRun(
                input,
                hasRun,
                runFirstCluster,
                runClusterCount,
                runVisibleClusterCount,
                runFirstIndex,
                runIndexCount,
                runMergedGapCount,
                runMergedGapIndexCount);
            continue;
        }

        if (gClusterCullEnableDebugCounters != 0u)
        {
            gClusterCullCounters.InterlockedAdd(
                HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_TESTED_COUNT,
                1);
        }

        float4 clusterWorldSphere = HikariClusterCullBuildWorldSphere(
            input.clusterWorld,
            cluster.sphereCenterRadius,
            cluster.boundsMin,
            cluster.boundsMax);
        if (!HikariClusterCullSphereVisible(clusterWorldSphere))
        {
            if (gClusterCullEnableDebugCounters != 0u)
            {
                gClusterCullCounters.InterlockedAdd(
                    HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_FRUSTUM_CULLED_COUNT,
                    1);
            }
            continue;
        }

        if (doubleSided)
        {
            if (gClusterCullEnableDebugCounters != 0u)
            {
                gClusterCullCounters.InterlockedAdd(
                    HIKARI_CLUSTER_CULL_COUNTER_DOUBLE_SIDED_CLUSTER_COUNT,
                    1);
            }
        }
        else if (gClusterCullEnableConeCull != 0u)
        {
            if (gClusterCullEnableDebugCounters != 0u)
            {
                gClusterCullCounters.InterlockedAdd(
                    HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_CONE_TESTED_COUNT,
                    1);
            }
            if (HikariClusterCullConeBackfacing(
                input.clusterWorld,
                cluster,
                clusterWorldSphere))
            {
                if (gClusterCullEnableDebugCounters != 0u)
                {
                    gClusterCullCounters.InterlockedAdd(
                        HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_CONE_CULLED_COUNT,
                        1);
                }
                continue;
            }
        }

        HikariClusterCullAppendVisibleCluster(
            input,
            clusterIndex,
            cluster,
            hasRun,
            runFirstCluster,
            runClusterCount,
            runVisibleClusterCount,
            runFirstIndex,
            runIndexCount,
            runMergedGapCount,
            runMergedGapIndexCount);
    }

    HikariClusterCullFlushVisibleRun(
        input,
        hasRun,
        runFirstCluster,
        runClusterCount,
        runVisibleClusterCount,
        runFirstIndex,
        runIndexCount,
        runMergedGapCount,
        runMergedGapIndexCount);
}

ClusterCullInput HikariClusterCullBuildInputFromPageTask(ClusterCullPageTask task)
{
    ClusterCullInput input = (ClusterCullInput)0;
    input.clusterWorld = task.clusterWorld;
    input.boundsCenterRadius = task.boundsCenterRadius;
    input.gpuSceneInstanceIndex = task.gpuSceneInstanceIndex;
    input.clusterGeometrySrvDescriptorIndex = task.clusterGeometrySrvDescriptorIndex;
    input.firstCluster = task.firstCluster;
    input.clusterCount = task.endCluster > task.firstCluster
        ? task.endCluster - task.firstCluster
        : 0u;
    input.clusterSurfaceIndex = task.clusterSurfaceIndex;
    input.passKind = task.passKind;
    input.flags = task.flags;
    input.clusterIndexCount = 0u;
    input.firstPage = task.pageIndex;
    input.pageCount = 1u;
    input.pageTaskBaseIndex = 0u;
    input.lodIndex = task.lodIndex;
    return input;
}

void HikariClusterCullEmitPageTasks(
    ClusterCullInput input,
    uint firstCluster,
    uint endCluster,
    uint firstPage,
    uint endPage)
{
    uint pageCount = endPage > firstPage ? endPage - firstPage : 0u;
    if (pageCount == 0u)
    {
        return;
    }

    uint taskBase = 0u;
    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT,
        pageCount,
        taskBase);
    if (taskBase >= gClusterCullPageTaskCapacity)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT,
            pageCount);
        return;
    }

    uint writableCount = min(pageCount, gClusterCullPageTaskCapacity - taskBase);
    if (writableCount < pageCount)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT,
            pageCount - writableCount);
    }

    for (uint pageOffset = 0u; pageOffset < writableCount; ++pageOffset)
    {
        ClusterCullPageTask task = (ClusterCullPageTask)0;
        task.clusterWorld = input.clusterWorld;
        task.boundsCenterRadius = input.boundsCenterRadius;
        task.gpuSceneInstanceIndex = input.gpuSceneInstanceIndex;
        task.clusterGeometrySrvDescriptorIndex = input.clusterGeometrySrvDescriptorIndex;
        task.firstCluster = firstCluster;
        task.endCluster = endCluster;
        task.clusterSurfaceIndex = input.clusterSurfaceIndex;
        task.passKind = input.passKind;
        task.flags = input.flags;
        task.pageIndex = firstPage + pageOffset;
        task.lodIndex = input.lodIndex;
        gClusterCullPageTasks[taskBase + pageOffset] = task;
    }
}

[numthreads(64, 1, 1)]
void ExpandPageTasksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localInstanceIndex = dispatchThreadId.x;
    if (localInstanceIndex >= gClusterCullInputCount)
    {
        return;
    }

    uint surfaceGpuSceneIndex =
        gClusterCullSurfaceGpuSceneBaseIndex + localInstanceIndex;
    HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
    if (!HikariClusterCullIsGpuSceneCandidate(instance))
    {
        return;
    }

    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_INPUT_COUNT,
        1);

    if (instance.clusterGeometrySrvDescriptorIndex < gClusterCullClusterSrvPoolBegin)
    {
        return;
    }

    uint clusterGeometryPoolIndex =
        instance.clusterGeometrySrvDescriptorIndex - gClusterCullClusterSrvPoolBegin;
    if (clusterGeometryPoolIndex >= gClusterCullClusterSrvPoolCount ||
        clusterGeometryPoolIndex >= 111u)
    {
        return;
    }

    ByteAddressBuffer geometry =
        gClusterGeometryPool[NonUniformResourceIndex(clusterGeometryPoolIndex)];
    HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(geometry);
    if (!HikariIsValidClusterGeometryHeader(header) ||
        instance.clusterRangeIndex >= header.clusterCount ||
        instance.clusterSurfaceIndex >= header.surfaceCount)
    {
        return;
    }

    HikariClusterGeometrySurface surface =
        HikariLoadClusterGeometrySurface(geometry, header, instance.clusterSurfaceIndex);
    if (surface.clusterCount == 0u ||
        surface.indexCount == 0u ||
        surface.firstLodRange >= header.surfaceLodRangeCount ||
        surface.lodRangeCount == 0u)
    {
        return;
    }

    HikariClusterGeometrySurfaceLodRange selectedRange;
    if (!HikariClusterCullSelectSurfaceLodRange(
            geometry,
            header,
            instance,
            selectedRange))
    {
        return;
    }

    ClusterCullInput input =
        HikariClusterCullBuildInput(surfaceGpuSceneIndex, instance, selectedRange);
    if (input.clusterIndexCount == 0u ||
        input.firstCluster >= header.clusterCount)
    {
        return;
    }

    if (!HikariClusterCullSphereVisible(input.boundsCenterRadius))
    {
        if (gClusterCullEnableDebugCounters != 0u)
        {
            gClusterCullCounters.InterlockedAdd(
                HIKARI_CLUSTER_CULL_COUNTER_INPUT_FRUSTUM_CULLED_COUNT,
                1);
        }
        return;
    }

    uint inputEndCluster = min(input.firstCluster + input.clusterCount, header.clusterCount);
    uint firstCluster = input.firstCluster;
    uint endCluster = inputEndCluster;
    if (firstCluster >= endCluster)
    {
        return;
    }

    uint inputPageEnd = min(input.firstPage + input.pageCount, header.pageCount);
    uint firstPage = input.firstPage;
    uint endPage = inputPageEnd;
    if (firstPage >= endPage)
    {
        return;
    }

    HikariClusterCullRecordSelectedLod(selectedRange.lodIndex);
    HikariClusterCullEmitPageTasks(
        input,
        firstCluster,
        endCluster,
        firstPage,
        endPage);
}

[numthreads(1, 1, 1)]
void FinalizePageTaskDispatchCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint pageTaskCount =
        gClusterCullCounters.Load(HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT);
    uint clampedTaskCount = min(pageTaskCount, gClusterCullPageTaskCapacity);
    uint groupCount =
        max(1u, (clampedTaskCount + 63u) / 64u);
    gClusterCullDispatchArguments[0] = uint3(groupCount, 1u, 1u);
}

[numthreads(1, 1, 1)]
void FinalizeMeshletDispatchCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    [unroll]
    for (uint bucket = 0u; bucket < 2u; ++bucket)
    {
        uint drawCounterOffset =
            HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_COUNT + bucket * 4u;
        uint drawCount = gClusterCullCounters.Load(drawCounterOffset);
        uint clampedDrawCount = min(drawCount, gClusterCullDrawArgumentBucketCapacity);
        uint bucketBase = bucket * gClusterCullDrawArgumentBucketCapacity;

        ClusterCullMeshletDispatchArgument meshletArgument;
        meshletArgument.rootConstants = uint4(
            bucketBase,
            gClusterCullPassKind,
            bucket,
            1u);
        meshletArgument.threadGroupCountX = clampedDrawCount;
        meshletArgument.threadGroupCountY = 1u;
        meshletArgument.threadGroupCountZ = 1u;
        meshletArgument.reserved0 = 0u;
        gClusterCullMeshletDispatchArguments[bucketBase] = meshletArgument;
    }
}

[numthreads(64, 1, 1)]
void CullPageTasksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint taskIndex = dispatchThreadId.x;
    uint pageTaskCount =
        gClusterCullCounters.Load(HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT);
    uint clampedTaskCount = min(pageTaskCount, gClusterCullPageTaskCapacity);
    if (taskIndex >= clampedTaskCount)
    {
        return;
    }

    ClusterCullPageTask task = gClusterCullPageTasks[taskIndex];
    if (task.clusterGeometrySrvDescriptorIndex < gClusterCullClusterSrvPoolBegin)
    {
        return;
    }

    uint clusterGeometryPoolIndex =
        task.clusterGeometrySrvDescriptorIndex - gClusterCullClusterSrvPoolBegin;
    if (clusterGeometryPoolIndex >= gClusterCullClusterSrvPoolCount ||
        clusterGeometryPoolIndex >= 111u)
    {
        return;
    }

    ByteAddressBuffer geometry =
        gClusterGeometryPool[NonUniformResourceIndex(clusterGeometryPoolIndex)];
    HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(geometry);
    if (!HikariIsValidClusterGeometryHeader(header) ||
        task.firstCluster >= task.endCluster ||
        task.firstCluster >= header.clusterCount ||
        task.clusterSurfaceIndex >= header.surfaceCount ||
        task.pageIndex >= header.pageCount)
    {
        return;
    }

    ClusterCullInput input =
        HikariClusterCullBuildInputFromPageTask(task);
    HikariClusterCullProcessPage(
        input,
        geometry,
        header,
        task.firstCluster,
        min(task.endCluster, header.clusterCount),
        task.pageIndex);
}
