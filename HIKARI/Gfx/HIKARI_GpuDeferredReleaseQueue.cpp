#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

#include "Core/HIKARI_Logger.h"

#include <utility>

namespace HIKARI::GFX {
	// GPU による遅延解放のためのキュークラス GpuDeferredReleaseQueue の実装
	// retireFenceValue と releaseCallback を受け取り、保留リストに追加する
    // retireFenceValue は、GPU がこのリリースを安全に実行できるようになるフェンスの値で、releaseCallback は実際のリリース処理を行うコールバック関数である
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
	// completedFenceValue を受け取り、保留リストの先頭から順に retireFenceValue が completedFenceValue 以下のアイテムを処理する
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
	// 保留リストのすべてのアイテムを処理する。GPU の完了を待たずにすべてのリリースを強制的に実行するために使用される
    void GpuDeferredReleaseQueue::FlushAll() {
        while (!pending_.empty()) {
            PendingRelease& item = pending_.front();
            if (item.releaseCallback) {
                item.releaseCallback();
            }
            pending_.pop_front();
        }
    }
	// 保留リストをクリアする。通常は、GPU の完了を待たずにすべてのリリースを強制的に実行した後に呼び出される
    void GpuDeferredReleaseQueue::Reset() {
        pending_.clear();
    }
	// 保留リストに現在存在するアイテムの数を返す
    size_t GpuDeferredReleaseQueue::GetPendingCount() const {
        return pending_.size();
    }

} // namespace HIKARI::GFX
