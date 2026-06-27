#include "HIKARI_ClusterCullCommon.hlsli"

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
