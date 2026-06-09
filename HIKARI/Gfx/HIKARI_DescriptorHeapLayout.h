#pragma once

#include <cstdint>
#include <d3d12.h>

namespace HIKARI::GFX::DESCRIPTOR {

    constexpr UINT kSrvHeapCapacity = 4096;
    constexpr UINT kSystemSrvReservedCount = 128;

    constexpr UINT kUserSrvBegin = 0;
    constexpr UINT kSystemSrvBegin = kSrvHeapCapacity - kSystemSrvReservedCount;
    constexpr UINT kUserSrvCount = kSystemSrvBegin - kUserSrvBegin;

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
        LightProbeSh = kSystemSrvBegin + 10,
        MeshObjectData = kSystemSrvBegin + 11,
        MeshMaterialData = kSystemSrvBegin + 12,
        ShadowObjectData = kSystemSrvBegin + 13,
    };

    constexpr UINT kSystemSrvUsedCount = 14;

    constexpr UINT ToIndex(SystemSrv slot) {
        return static_cast<UINT>(slot);
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

    static_assert(kSystemSrvReservedCount >= kSystemSrvUsedCount);
    static_assert(kSystemSrvBegin < kSrvHeapCapacity);
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
    static_assert(ToIndex(SystemSrv::LightProbeSh) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshObjectData) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::MeshMaterialData) < kSrvHeapCapacity);
    static_assert(ToIndex(SystemSrv::ShadowObjectData) < kSrvHeapCapacity);

} // namespace HIKARI::GFX::DESCRIPTOR
