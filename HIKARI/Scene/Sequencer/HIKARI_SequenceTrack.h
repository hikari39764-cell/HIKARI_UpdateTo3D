#pragma once

#include <cstdint>

namespace HIKARI::SEQUENCER {

    struct SequenceTrackId {
        uint64_t value = 0;

        bool IsValid() const noexcept {
            return value != 0;
        }
    };

    enum class SequenceInterpolationMode : uint8_t {
        Hold,
        Linear,
        Smooth,
    };

    constexpr float kMaxSequenceDurationSeconds = 3600.0f;

} // namespace HIKARI::SEQUENCER
