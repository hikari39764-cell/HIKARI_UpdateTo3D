#pragma once

#include <d3d12.h>

namespace HIKARI::SHADOW::RECORD {

    constexpr UINT kShadowStaticRootParamCamera = 0;
    constexpr UINT kShadowStaticRootParamObject = 1;
    constexpr UINT kShadowStaticRootParamBaseColorTexture = 2;
    constexpr UINT kShadowStaticRootParamMaterialData = 3;
    constexpr UINT kShadowStaticRootParamSurfaceGpuScene = 4;
    constexpr UINT kShadowStaticRootParamSurfaceGpuSceneControl = 5;
    constexpr UINT kShadowStaticRootParamTexturePool = 6;
    constexpr UINT kShadowStaticRootParamMaterialIndex = 7;
    constexpr UINT kShadowStaticRootParamObjectData = 8;
    constexpr UINT kShadowStaticRootParamClusterGeometryPool = 9;
    constexpr UINT kShadowStaticRootParamMeshletVisibleRanges = 10;
    constexpr UINT kShadowStaticRootParamCullingCamera = 11;
    constexpr UINT kShadowSkinnedRootParamJointPalette = 12;

    bool InitializeShadowRecordExecutor(ID3D12Device* device);
    void ResetShadowRecordExecutor();

} // namespace HIKARI::SHADOW::RECORD
