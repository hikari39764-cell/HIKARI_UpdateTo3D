#pragma once

#include <cstdint>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"
#include "Scene/Sequencer/HIKARI_SequencePlayback.h"

namespace HIKARI::SEQUENCER {

    struct SequencePlaybackHandle {
        uint64_t value = 0;
        uint64_t epoch = 0;

        bool IsValid() const noexcept {
            return value != 0 && epoch != 0;
        }

        bool operator==(const SequencePlaybackHandle& rhs) const noexcept =
            default;
    };

    enum class SequenceStopReason : uint8_t {
        Stopped,
        Replaced,
        Completed,
        Reset,
    };

    struct SequencePlayRequest {
        AssetGuid assetGuid{};
        SequenceBindingContext bindings{};
        SequencePlaybackOptions options{};
        std::string channel{};
        float startTimeSeconds = 0.0f;
        int priority = 0;
        bool replaceChannel = true;
    };

    enum class SequencePlaybackEventKind : uint8_t {
        Started,
        Paused,
        Resumed,
        Seeked,
        Completed,
        Stopped,
        Rejected,
    };

    struct SequencePlaybackEvent {
        SequencePlaybackEventKind kind =
            SequencePlaybackEventKind::Rejected;
        SequencePlaybackHandle handle{};
        AssetGuid assetGuid{};
        uint64_t requestId = 0;
        float timeSeconds = 0.0f;
        std::string message{};
    };

    enum class SequencePlaybackCommandKind : uint8_t {
        Play,
        Stop,
        Pause,
        Resume,
        Seek,
    };

    struct SequencePlaybackCommand {
        SequencePlaybackCommandKind kind =
            SequencePlaybackCommandKind::Play;
        uint64_t requestId = 0;
        SequencePlayRequest play{};
        SequencePlaybackHandle handle{};
        float timeSeconds = 0.0f;
    };

} // namespace HIKARI::SEQUENCER
