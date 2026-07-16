#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"

namespace HIKARI {

    class GameObject;
    class SequencePlayerComponent;

namespace SEQUENCER {
    class SequencePlaybackService;
}

    class SequencePlayerSystem final : public ISystem {
    public:
        explicit SequencePlayerSystem(
            SEQUENCER::SequencePlaybackService& playbackService,
            const bool& runtimePlayActive) noexcept;

        std::string_view GetName() const override {
            return "SequencePlayerSystem";
        }
        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;

    private:
        void ProcessComponent(
            GameObject& owner,
            SequencePlayerComponent& component);
        void PlayComponent(
            GameObject& owner,
            SequencePlayerComponent& component);
        void StopComponent(
            SequencePlayerComponent& component,
            SEQUENCER::SequenceStopReason reason);

        SEQUENCER::SequencePlaybackService* playbackService_ = nullptr;
        const bool* runtimePlayActive_ = nullptr;
        std::vector<SEQUENCER::SequencePlaybackHandle> trackedHandles_{};
    };

} // namespace HIKARI
