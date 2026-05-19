#pragma once

#include <unordered_map>

#include <d3d12.h>
#include <d3dx12.h>

namespace HIKARI::RENDER3D {

    class ResourceStateTracker {
    public:
        void Reset();
        void Track(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState);
        void Transition(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after);

    private:
        std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> knownStates_;
    };

} // namespace HIKARI::RENDER3D
