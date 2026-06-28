#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

struct IUnknown;

namespace HIKARI::GFX {

    class GpuDeferredReleaseQueue {
    public:
        void Enqueue(
            uint64_t retireFenceValue,
            std::function<void()> releaseCallback,
            std::string debugName = {});

        void Collect(uint64_t completedFenceValue);
        void FlushAll();
        void Reset();

        size_t GetPendingCount() const;

    private:
        struct PendingRelease {
            uint64_t retireFenceValue = 0;
            std::function<void()> releaseCallback;
            std::string debugName;
        };

        std::deque<PendingRelease> pending_;
    };

    void RetireD3D12ObjectForCurrentFrame(
        IUnknown* object,
        std::string debugName = {});

} // namespace HIKARI::GFX
