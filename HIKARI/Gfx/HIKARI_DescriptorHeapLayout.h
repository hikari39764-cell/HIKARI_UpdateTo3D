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
    };

    constexpr UINT kSystemSrvUsedCount = 4;

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

} // namespace HIKARI::GFX::DESCRIPTOR
