#include "Gfx/HIKARI_ResourceStateTracker.h"

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"

#include <cassert>
#include <cstdint>
#include <sstream>

namespace {

    std::string DescribeResource(ID3D12Resource* resource) {
        std::ostringstream oss;
        oss << "resource=0x" << std::hex << reinterpret_cast<uintptr_t>(resource);
        return oss.str();
    }

}

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
        if (cmd == nullptr || resource == nullptr) {
            return;
        }

        const auto found = knownStates_.find(resource);
        if (found == knownStates_.end()) {
            const std::string message =
                "[ResourceStateTracker][ERROR] Transition called for untracked resource. " +
                DescribeResource(resource);
            DEBUGLOG::PushRenderError(message);
            HIKARI_LOG_ERROR(message);
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
        if (cmd == nullptr || resource == nullptr) {
            return;
        }

        const auto found = knownStates_.find(resource);
        if (found != knownStates_.end() && found->second != before) {
            std::ostringstream oss;
            oss << "[ResourceStateTracker][ERROR] before state mismatch. "
                << DescribeResource(resource)
                << " tracked=" << GFX::ResourceStateToString(found->second)
                << " requestedBefore=" << GFX::ResourceStateToString(before)
                << " requestedAfter=" << GFX::ResourceStateToString(after);

            const std::string message = oss.str();
            DEBUGLOG::PushRenderError(message);
            HIKARI_LOG_ERROR(message);
            assert(false && "ResourceStateTracker::Transition before state mismatch.");
        }

        if (before == after) {
            knownStates_[resource] = after;
            return;
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
        cmd->ResourceBarrier(1, &barrier);
        knownStates_[resource] = after;
    }

} // namespace HIKARI::GFX
