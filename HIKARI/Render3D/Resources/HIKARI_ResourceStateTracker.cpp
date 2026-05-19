#include "Render3D/Resources/HIKARI_ResourceStateTracker.h"

namespace HIKARI::RENDER3D {

    void ResourceStateTracker::Reset() {
        knownStates_.clear();
    }

    void ResourceStateTracker::Track(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState) {
        if (resource == nullptr) {
            return;
        }
        knownStates_[resource] = initialState;
    }

    void ResourceStateTracker::Transition(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) {
        if (cmd == nullptr || resource == nullptr || before == after) {
            return;
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
        cmd->ResourceBarrier(1, &barrier);
        knownStates_[resource] = after;
    }

} // namespace HIKARI::RENDER3D
