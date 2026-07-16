#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "Assets/Sequence/HIKARI_SequenceAssetStore.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequenceTrackDriver.h"

namespace HIKARI::SEQUENCER {

    class SequencePlaybackService {
    public:
        void SetAssetStore(SequenceAssetStore* assetStore) noexcept;
        bool RegisterDriver(std::unique_ptr<ISequenceTrackDriver> driver);

        SequencePlaybackHandle Play(
            const SequencePlayRequest& request);
        SequencePlaybackHandle PlayInline(
            const CinematicSequence& sequence,
            const SequenceBindingContext& bindings = {},
            const SequencePlaybackOptions& options = {},
            float startTimeSeconds = 0.0f,
            std::string channel = {},
            int priority = 0,
            bool replaceChannel = true);
        bool Stop(SequencePlaybackHandle handle);
        bool Pause(SequencePlaybackHandle handle);
        bool Resume(SequencePlaybackHandle handle);
        bool Seek(SequencePlaybackHandle handle, float timeSeconds);

        uint64_t Submit(SequencePlaybackCommand command);
        void Update(float deltaTime);
        void Reset();

        bool IsPlaying(SequencePlaybackHandle handle) const noexcept;
        bool IsPaused(SequencePlaybackHandle handle) const noexcept;
        float GetTimeSeconds(SequencePlaybackHandle handle) const noexcept;
        size_t GetActiveInstanceCount() const noexcept;
        std::vector<SequencePlaybackEvent> ConsumeEvents();

    private:
        struct Instance {
            SequencePlaybackHandle handle{};
            std::shared_ptr<const SequenceAsset> asset{};
            SequenceBindingContext bindings{};
            SequencePlaybackCursor cursor{};
            std::string channel{};
            int priority = 0;
            uint64_t requestId = 0;
        };

        SequencePlaybackHandle PlayLoaded(
            std::shared_ptr<const SequenceAsset> asset,
            const SequencePlayRequest& request,
            uint64_t requestId);
        bool StopInternal(
            SequencePlaybackHandle handle,
            SequenceStopReason reason,
            uint64_t requestId);
        bool PauseInternal(
            SequencePlaybackHandle handle,
            uint64_t requestId);
        bool ResumeInternal(
            SequencePlaybackHandle handle,
            uint64_t requestId);
        bool SeekInternal(
            SequencePlaybackHandle handle,
            float timeSeconds,
            uint64_t requestId);
        void FlushCommands();
        void EvaluateInstance(
            Instance& instance,
            const SequenceEvaluationContext& context);
        SequencePlaybackInstanceView MakeView(
            const Instance& instance) const noexcept;
        size_t FindInstanceIndex(
            SequencePlaybackHandle handle) const noexcept;
        void StopInstanceAt(
            size_t index,
            SequenceStopReason reason,
            uint64_t requestId);
        void PushRejected(
            uint64_t requestId,
            const AssetGuid& assetGuid,
            std::string message);
        void AdvanceEpoch() noexcept;

        SequenceAssetStore* assetStore_ = nullptr;
        std::vector<std::unique_ptr<ISequenceTrackDriver>> drivers_{};
        std::vector<Instance> instances_{};
        std::vector<SequencePlaybackCommand> pendingCommands_{};
        std::vector<SequencePlaybackEvent> events_{};
        uint64_t nextHandleValue_ = 1;
        uint64_t nextRequestId_ = 1;
        uint64_t epoch_ = 1;
    };

} // namespace HIKARI::SEQUENCER
