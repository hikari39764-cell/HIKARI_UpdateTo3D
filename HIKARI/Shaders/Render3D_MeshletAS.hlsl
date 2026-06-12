#include "Include/HIKARI_MeshletDraw.hlsli"

struct HikariMeshletPayload
{
    uint visibleRangeIndex;
};

[numthreads(1, 1, 1)]
void main(uint3 groupId : SV_GroupID)
{
    HikariMeshletPayload payload;
    payload.visibleRangeIndex = gMeshletVisibleRangeIndex + groupId.x;

    HikariMeshletVisibleRange visible = gMeshletVisibleRanges[payload.visibleRangeIndex];
    uint meshGroupCount = max(visible.clusterCount, 1u);
    DispatchMesh(meshGroupCount, 1u, 1u, payload);
}
