#include "Include/HIKARI_MeshletDraw.hlsli"

struct HikariMeshletPayload
{
    uint visibleRangeIndex;
    uint clusterCount;
};

[numthreads(1, 1, 1)]
void main(uint3 groupId : SV_GroupID)
{
    HikariMeshletPayload payload;
    payload.visibleRangeIndex = gMeshletVisibleRangeIndex + groupId.x;

    HikariMeshletVisibleRange visible = gMeshletVisibleRanges[payload.visibleRangeIndex];
    bool rangeValid =
        visible.clusterCount != 0u &&
        visible.clusterGeometrySrvDescriptorIndex != 0xffffffffu;
    payload.clusterCount = rangeValid ? visible.clusterCount : 0u;
    uint meshGroupCount = max(payload.clusterCount, 1u);
    DispatchMesh(meshGroupCount, 1u, 1u, payload);
}
