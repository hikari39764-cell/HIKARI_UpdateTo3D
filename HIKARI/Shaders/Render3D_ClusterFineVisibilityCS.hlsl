#include "HIKARI_ClusterCullCommon.hlsli"

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
    if (task.clusterGeometryMetadataSrvDescriptorIndex < gClusterCullClusterSrvPoolBegin)
    {
        return;
    }

    uint clusterMetadataPoolIndex =
        task.clusterGeometryMetadataSrvDescriptorIndex - gClusterCullClusterSrvPoolBegin;
    if (clusterMetadataPoolIndex >= gClusterCullClusterSrvPoolCount)
    {
        return;
    }

    ByteAddressBuffer metadata =
        gClusterGeometryPool[NonUniformResourceIndex(clusterMetadataPoolIndex)];
    HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(metadata);
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
    HikariClusterCullProcessPageGroup(
        input,
        metadata,
        header,
        task.firstCluster,
        min(task.endCluster, header.clusterCount),
        task.pageIndex,
        max(task.reserved0, 1u));
}
