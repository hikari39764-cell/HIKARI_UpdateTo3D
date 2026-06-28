#pragma once

#include <cstdint>
#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::GFX::DESCRIPTOR {

    constexpr UINT kUserSrvCount = 3968;
    constexpr UINT kSystemSrvFixedCount = 33;
    constexpr UINT kSystemSrvDynamicCount = 111;
    constexpr UINT kGpuDepthVisibilityTransientDescriptorCount = 384;
    constexpr UINT kSystemSrvAuxUsedCount = kGpuDepthVisibilityTransientDescriptorCount + 1;
    constexpr UINT kSystemSrvReservedCount =
        kSystemSrvFixedCount + kSystemSrvDynamicCount + kSystemSrvAuxUsedCount;
    constexpr UINT kSrvHeapCapacity = kUserSrvCount + kSystemSrvReservedCount;

    constexpr UINT kUserSrvBegin = 0;
    constexpr UINT kSystemSrvBegin = kSrvHeapCapacity - kSystemSrvReservedCount;

    enum class SystemSrv : UINT {
        SceneColor = kSystemSrvBegin + 0,
        SceneDepth = kSystemSrvBegin + 1,
        EditorViewport = kSystemSrvBegin + 2,
        ImGuiFont = kSystemSrvBegin + 3,

        IblIrradiance = kSystemSrvBegin + 4,
        IblPrefiltered = kSystemSrvBegin + 5,
        IblBrdfLut = kSystemSrvBegin + 6,
        SceneNormalRoughness = kSystemSrvBegin + 7,
        SsaoRaw = kSystemSrvBegin + 8,
        SsaoBlurred = kSystemSrvBegin + 9,
        SsaoResolved = kSystemSrvBegin + 10,
        LightProbeSh = kSystemSrvBegin + 11,
        MeshObjectData = kSystemSrvBegin + 12,
        MeshMaterialData = kSystemSrvBegin + 13,
        MeshSurfaceGpuScene = kSystemSrvBegin + 14,
        ShadowSurfaceGpuScene = kSystemSrvBegin + 15,
        ShadowMaterialData = kSystemSrvBegin + 16,
        PostSceneDepth = kSystemSrvBegin + 17,

        MeshObjectDataFrame0 = kSystemSrvBegin + 18,
        MeshObjectDataFrame1 = kSystemSrvBegin + 19,
        MeshObjectDataFrame2 = kSystemSrvBegin + 20,
        MeshMaterialDataFrame0 = kSystemSrvBegin + 21,
        MeshMaterialDataFrame1 = kSystemSrvBegin + 22,
        MeshMaterialDataFrame2 = kSystemSrvBegin + 23,
        MeshSurfaceGpuSceneFrame0 = kSystemSrvBegin + 24,
        MeshSurfaceGpuSceneFrame1 = kSystemSrvBegin + 25,
        MeshSurfaceGpuSceneFrame2 = kSystemSrvBegin + 26,
        ShadowSurfaceGpuSceneFrame0 = kSystemSrvBegin + 27,
        ShadowSurfaceGpuSceneFrame1 = kSystemSrvBegin + 28,
        ShadowSurfaceGpuSceneFrame2 = kSystemSrvBegin + 29,
        ShadowMaterialDataFrame0 = kSystemSrvBegin + 30,
        ShadowMaterialDataFrame1 = kSystemSrvBegin + 31,
        ShadowMaterialDataFrame2 = kSystemSrvBegin + 32,
    };

    constexpr UINT kSystemSrvUsedCount = kSystemSrvFixedCount;
    constexpr UINT kSystemSrvDynamicBegin = kSystemSrvBegin + kSystemSrvUsedCount;
    constexpr UINT kSystemSrvAuxBegin = kSystemSrvDynamicBegin + kSystemSrvDynamicCount;
    constexpr UINT kGpuDepthVisibilityTransientDescriptorBegin = kSystemSrvAuxBegin;
    constexpr UINT kGpuDepthVisibilityTransientDescriptorEnd =
        kGpuDepthVisibilityTransientDescriptorBegin +
        kGpuDepthVisibilityTransientDescriptorCount;
    constexpr UINT kClusterCullFallbackHzbSrv =
        kGpuDepthVisibilityTransientDescriptorEnd;

    constexpr UINT ToIndex(SystemSrv slot) {
        return static_cast<UINT>(slot);
    }

    constexpr UINT ToFrameIndex(SystemSrv firstFrameSlot, uint32_t frameIndex) {
        return ToIndex(firstFrameSlot) + (frameIndex % GFX::kFrameResourceCount);
    }

    inline D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(
        ID3D12DescriptorHeap* heap,
        UINT descriptorSize,
        UINT index) {

        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (heap == nullptr) {
            return handle;
        }

        handle = heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptorSize) * index;
        return handle;
    }

    inline D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(
        ID3D12DescriptorHeap* heap,
        UINT descriptorSize,
        UINT index) {

        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (heap == nullptr) {
            return handle;
        }

        handle = heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(descriptorSize) * index;
        return handle;
    }

    static_assert(
        kSystemSrvReservedCount >=
        kSystemSrvUsedCount + kSystemSrvDynamicCount + kSystemSrvAuxUsedCount);
    static_assert(kUserSrvCount == 3968);
    static_assert(kSystemSrvDynamicBegin == 4001);
    static_assert(kSystemSrvDynamicCount == 111);
    static_assert(kSystemSrvAuxBegin == 4112);
    static_assert(kSystemSrvBegin < kSrvHeapCapacity);
    static_assert(kSystemSrvDynamicBegin < kSrvHeapCapacity);
    static_assert(kSystemSrvDynamicBegin + kSystemSrvDynamicCount <= kSrvHeapCapacity);
    static_assert(kGpuDepthVisibilityTransientDescriptorBegin < kSrvHeapCapacity);
    static_assert(kGpuDepthVisibilityTransientDescriptorEnd <= kSrvHeapCapacity);
    static_assert(kClusterCullFallbackHzbSrv < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::SceneColor) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::SceneDepth) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::EditorViewport) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ImGuiFont) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::IblIrradiance) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::IblPrefiltered) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::IblBrdfLut) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::SceneNormalRoughness) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::SsaoRaw) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::SsaoBlurred) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::SsaoResolved) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::LightProbeSh) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshObjectData) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshMaterialData) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshSurfaceGpuScene) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ShadowSurfaceGpuScene) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ShadowMaterialData) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::PostSceneDepth) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshObjectDataFrame0) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshObjectDataFrame2) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshMaterialDataFrame0) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshMaterialDataFrame2) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshSurfaceGpuSceneFrame0) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshSurfaceGpuSceneFrame2) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ShadowSurfaceGpuSceneFrame0) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ShadowSurfaceGpuSceneFrame2) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ShadowMaterialDataFrame0) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ShadowMaterialDataFrame2) < kSrvHeapCapacity);

} // namespace HIKARI::GFX::DESCRIPTOR
