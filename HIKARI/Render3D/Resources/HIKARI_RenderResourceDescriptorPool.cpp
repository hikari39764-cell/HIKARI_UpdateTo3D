#include "Render3D/Resources/HIKARI_RenderResourceDescriptorPool.h"

#include "Gfx/HIKARI_DescriptorAllocator.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"

namespace HIKARI::RENDER3D {

    namespace {

        struct RenderResourceDescriptorPoolState {
            GFX::Context context{};
            GFX::DescriptorAllocator allocator{};
            UINT descriptorSize = 0;
            bool initialized = false;
            uint32_t allocationCount = 0;
            uint32_t freeCount = 0;
            uint32_t failedAllocationCount = 0;
        };

        RenderResourceDescriptorPoolState& State() {
            static RenderResourceDescriptorPoolState state{};
            return state;
        }

        bool HasValidContext(const GFX::Context& ctx) {
            return ctx.device != nullptr && ctx.srvHeap != nullptr;
        }

        void RefreshContext(RenderResourceDescriptorPoolState& state, const GFX::Context& ctx) {
            state.context = ctx;
            if (!HasValidContext(ctx)) {
                return;
            }

            state.descriptorSize = ctx.device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

    } // namespace

    void UpdateRenderResourceDescriptorPoolContext(const GFX::Context& ctx) {
        RenderResourceDescriptorPoolState& state = State();
        RefreshContext(state, ctx);

        if (state.initialized || !HasValidContext(ctx)) {
            return;
        }

        // リソース層のSRVは、固定システムSRVの後ろにある動的領域だけを使う。
        state.allocator.Initialize(
            GFX::DESCRIPTOR::kSystemSrvDynamicBegin,
            GFX::DESCRIPTOR::kSystemSrvDynamicCount);
        state.initialized = true;
    }

    void ShutdownRenderResourceDescriptorPool() {
        RenderResourceDescriptorPoolState& state = State();
        state = {};
    }

    RenderResourceView AllocateBufferSrvDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT numElements,
        UINT structureByteStride,
        D3D12_BUFFER_SRV_FLAGS flags) {

        RenderResourceDescriptorPoolState& state = State();
        if (!state.initialized ||
            !HasValidContext(state.context) ||
            resource == nullptr ||
            numElements == 0) {
            ++state.failedAllocationCount;
            return {};
        }

        const GFX::DescriptorSlot slot = state.allocator.Allocate();
        if (!slot.IsValid()) {
            ++state.failedAllocationCount;
            return {};
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numElements;
        srvDesc.Buffer.StructureByteStride = structureByteStride;
        srvDesc.Buffer.Flags = flags;

        const D3D12_CPU_DESCRIPTOR_HANDLE cpu =
            GFX::DESCRIPTOR::CpuAt(state.context.srvHeap, state.descriptorSize, slot.index);
        const D3D12_GPU_DESCRIPTOR_HANDLE gpu =
            GFX::DESCRIPTOR::GpuAt(state.context.srvHeap, state.descriptorSize, slot.index);

        state.context.device->CreateShaderResourceView(resource, &srvDesc, cpu);

        RenderResourceView view{};
        view.descriptorIndex = slot.index;
        view.cpu = cpu;
        view.gpu = gpu;
        ++state.allocationCount;
        return view;
    }

    bool ReleaseRenderResourceDescriptor(RenderResourceView view) {
        RenderResourceDescriptorPoolState& state = State();
        if (!state.initialized || view.descriptorIndex == UINT32_MAX) {
            return false;
        }

        const GFX::DescriptorSlot slot{ view.descriptorIndex };
        if (!state.allocator.Owns(slot) || !state.allocator.IsAllocated(slot)) {
            return false;
        }

        state.allocator.Free(slot);
        ++state.freeCount;
        return true;
    }

    RenderResourceDescriptorPoolStats GetRenderResourceDescriptorPoolStats() {
        const RenderResourceDescriptorPoolState& state = State();

        RenderResourceDescriptorPoolStats stats{};
        stats.initialized = state.initialized && HasValidContext(state.context);
        stats.begin = state.allocator.GetBegin();
        stats.capacity = state.allocator.GetCount();
        stats.used = state.allocator.GetUsedCount();
        stats.free = state.allocator.GetFreeCount();
        stats.allocationCount = state.allocationCount;
        stats.freeCount = state.freeCount;
        stats.failedAllocationCount = state.failedAllocationCount;
        return stats;
    }

} // namespace HIKARI::RENDER3D
