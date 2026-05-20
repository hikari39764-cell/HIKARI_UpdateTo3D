#include "Gfx/HIKARI_ResourceStateTracker.h"

#include <cassert>

namespace HIKARI::GFX {

    void ResourceStateTracker::Reset() {
        knownStates_.clear();
    }

    void ResourceStateTracker::Track(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES initialState) {
        if (resource == nullptr) {
            return;
        }

        knownStates_[resource] = initialState;
    }

    void ResourceStateTracker::Forget(ID3D12Resource* resource) {
        if (resource == nullptr) {
            return;
        }

        knownStates_.erase(resource);
    }

    bool ResourceStateTracker::Has(ID3D12Resource* resource) const {
        return resource != nullptr && knownStates_.find(resource) != knownStates_.end();
    }

    D3D12_RESOURCE_STATES ResourceStateTracker::GetState(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES fallback) const {
        const auto found = knownStates_.find(resource);
        if (found == knownStates_.end()) {
            return fallback;
        }

        return found->second;
    }

    void ResourceStateTracker::Transition(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES after) {
        const auto found = knownStates_.find(resource);
        if (found == knownStates_.end()) {
            assert(false && "ResourceStateTracker::Transition called for an untracked resource.");
            return;
        }

        Transition(cmd, resource, found->second, after);
    }

    void ResourceStateTracker::Transition(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) {
        if (cmd == nullptr || resource == nullptr || before == after) {
            return;
        }

        const auto found = knownStates_.find(resource);
        if (found != knownStates_.end()) {
            assert(found->second == before && "ResourceStateTracker::Transition before state mismatch.");
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
        cmd->ResourceBarrier(1, &barrier);
        knownStates_[resource] = after;
    }

} // namespace HIKARI::GFX
