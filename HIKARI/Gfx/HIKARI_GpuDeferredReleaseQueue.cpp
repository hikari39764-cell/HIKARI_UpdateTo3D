#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

#include "Core/HIKARI_Logger.h"

#include <utility>

namespace HIKARI::GFX {

    void GpuDeferredReleaseQueue::Enqueue(
        uint64_t retireFenceValue,
        std::function<void()> releaseCallback,
        std::string debugName) {

        if (!releaseCallback) {
            return;
        }

        PendingRelease item{};
        item.retireFenceValue = retireFenceValue;
        item.releaseCallback = std::move(releaseCallback);
        item.debugName = std::move(debugName);
        pending_.push_back(std::move(item));
    }

    void GpuDeferredReleaseQueue::Collect(uint64_t completedFenceValue) {
        while (!pending_.empty()) {
            PendingRelease& item = pending_.front();
            if (item.retireFenceValue > completedFenceValue) {
                break;
            }

            if (item.releaseCallback) {
                item.releaseCallback();
            }
            pending_.pop_front();
        }
    }

    void GpuDeferredReleaseQueue::FlushAll() {
        while (!pending_.empty()) {
            PendingRelease& item = pending_.front();
            if (item.releaseCallback) {
                item.releaseCallback();
            }
            pending_.pop_front();
        }
    }

    void GpuDeferredReleaseQueue::Reset() {
        pending_.clear();
    }

    size_t GpuDeferredReleaseQueue::GetPendingCount() const {
        return pending_.size();
    }

} // namespace HIKARI::GFX
