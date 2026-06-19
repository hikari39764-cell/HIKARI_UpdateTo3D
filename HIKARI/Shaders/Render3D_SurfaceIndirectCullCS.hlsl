struct SurfaceIndirectDrawArgument
{
    uint2 vertexBufferLocation;
    uint vertexBufferSizeInBytes;
    uint vertexBufferStrideInBytes;

    uint2 indexBufferLocation;
    uint indexBufferSizeInBytes;
    uint indexBufferFormat;

    uint4 rootConstants;

    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

struct SurfaceIndirectDrawSeed
{
    SurfaceIndirectDrawArgument argument;
    float4 boundsCenterRadius;
    uint absoluteGpuSceneInstanceIndex;
    uint flags;
    uint reserved0;
    uint reserved1;
};

cbuffer SurfaceIndirectCullingCB : register(b0)
{
    float4x4 gSurfaceIndirectViewProj;
    uint gSurfaceIndirectInputCount;
    uint gSurfaceIndirectOutputCapacity;
    uint gSurfaceIndirectEnableFrustumCull;
    uint gSurfaceIndirectReserved0;
};

StructuredBuffer<SurfaceIndirectDrawSeed> gSurfaceIndirectSeeds : register(t0);
RWStructuredBuffer<SurfaceIndirectDrawArgument> gSurfaceIndirectArguments : register(u0);
RWByteAddressBuffer gSurfaceIndirectCounters : register(u1);

static const uint HIKARI_SURFACE_INDIRECT_COUNTER_DRAW_COUNT = 0u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_VISIBLE_COUNT = 4u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_CULLED_COUNT = 8u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_OVERFLOW_COUNT = 12u;

float4 HikariSurfaceIndirectViewProjRow0()
{
    return float4(
        gSurfaceIndirectViewProj._11,
        gSurfaceIndirectViewProj._12,
        gSurfaceIndirectViewProj._13,
        gSurfaceIndirectViewProj._14);
}

float4 HikariSurfaceIndirectViewProjRow1()
{
    return float4(
        gSurfaceIndirectViewProj._21,
        gSurfaceIndirectViewProj._22,
        gSurfaceIndirectViewProj._23,
        gSurfaceIndirectViewProj._24);
}

float4 HikariSurfaceIndirectViewProjRow2()
{
    return float4(
        gSurfaceIndirectViewProj._31,
        gSurfaceIndirectViewProj._32,
        gSurfaceIndirectViewProj._33,
        gSurfaceIndirectViewProj._34);
}

float4 HikariSurfaceIndirectViewProjRow3()
{
    return float4(
        gSurfaceIndirectViewProj._41,
        gSurfaceIndirectViewProj._42,
        gSurfaceIndirectViewProj._43,
        gSurfaceIndirectViewProj._44);
}

bool HikariSurfaceIndirectPlaneVisible(float4 plane, float3 center, float radius)
{
    const float planeLength = length(plane.xyz);
    if (planeLength <= 0.000001f)
    {
        return true;
    }
    return dot(plane.xyz, center) + plane.w >= -radius * planeLength;
}

bool HikariSurfaceIndirectFrustumVisible(float4 boundsCenterRadius)
{
    const float radius = boundsCenterRadius.w;
    if (radius < 0.0f)
    {
        return true;
    }

    const float3 center = boundsCenterRadius.xyz;
    const float4 row0 = HikariSurfaceIndirectViewProjRow0();
    const float4 row1 = HikariSurfaceIndirectViewProjRow1();
    const float4 row2 = HikariSurfaceIndirectViewProjRow2();
    const float4 row3 = HikariSurfaceIndirectViewProjRow3();

    return
        HikariSurfaceIndirectPlaneVisible(row3 + row0, center, radius) &&
        HikariSurfaceIndirectPlaneVisible(row3 - row0, center, radius) &&
        HikariSurfaceIndirectPlaneVisible(row3 + row1, center, radius) &&
        HikariSurfaceIndirectPlaneVisible(row3 - row1, center, radius) &&
        HikariSurfaceIndirectPlaneVisible(row2, center, radius) &&
        HikariSurfaceIndirectPlaneVisible(row3 - row2, center, radius);
}

[numthreads(64, 1, 1)]
void CompactSurfaceIndirectCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint seedIndex = dispatchThreadId.x;
    if (seedIndex >= gSurfaceIndirectInputCount)
    {
        return;
    }

    const SurfaceIndirectDrawSeed seed = gSurfaceIndirectSeeds[seedIndex];
    const bool hasVertexBuffer =
        seed.argument.vertexBufferLocation.x != 0u ||
        seed.argument.vertexBufferLocation.y != 0u;
    const bool hasIndexBuffer =
        seed.argument.indexBufferLocation.x != 0u ||
        seed.argument.indexBufferLocation.y != 0u;
    if (seed.argument.indexCountPerInstance == 0u ||
        seed.argument.instanceCount == 0u ||
        !hasVertexBuffer ||
        !hasIndexBuffer)
    {
        uint ignored = 0u;
        gSurfaceIndirectCounters.InterlockedAdd(
            HIKARI_SURFACE_INDIRECT_COUNTER_CULLED_COUNT,
            1u,
            ignored);
        return;
    }

    const bool visible =
        gSurfaceIndirectEnableFrustumCull == 0u ||
        HikariSurfaceIndirectFrustumVisible(seed.boundsCenterRadius);
    if (!visible)
    {
        uint ignored = 0u;
        gSurfaceIndirectCounters.InterlockedAdd(
            HIKARI_SURFACE_INDIRECT_COUNTER_CULLED_COUNT,
            1u,
            ignored);
        return;
    }

    uint visibleIndex = 0u;
    gSurfaceIndirectCounters.InterlockedAdd(
        HIKARI_SURFACE_INDIRECT_COUNTER_DRAW_COUNT,
        1u,
        visibleIndex);

    uint ignoredVisible = 0u;
    gSurfaceIndirectCounters.InterlockedAdd(
        HIKARI_SURFACE_INDIRECT_COUNTER_VISIBLE_COUNT,
        1u,
        ignoredVisible);

    if (visibleIndex >= gSurfaceIndirectOutputCapacity)
    {
        uint ignoredOverflow = 0u;
        gSurfaceIndirectCounters.InterlockedAdd(
            HIKARI_SURFACE_INDIRECT_COUNTER_OVERFLOW_COUNT,
            1u,
            ignoredOverflow);
        return;
    }

    SurfaceIndirectDrawArgument argument = seed.argument;
    argument.rootConstants.x = seed.absoluteGpuSceneInstanceIndex;
    argument.rootConstants.y = 1u;
    argument.instanceCount = 1u;
    argument.startInstanceLocation = 0u;
    gSurfaceIndirectArguments[visibleIndex] = argument;
}
