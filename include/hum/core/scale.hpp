#pragma once

#include "hum/core/midi.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace hum::core {

struct ScaleDefinition {
    std::uint8_t root_pitch_class{0};
    std::vector<std::uint8_t> intervals{0, 2, 4, 5, 7, 9, 11};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool contains(std::uint8_t midi_pitch) const noexcept;
    [[nodiscard]] std::uint8_t nearestPitch(std::uint8_t midi_pitch) const noexcept;
};

class IScaleProcessor {
public:
    virtual ~IScaleProcessor() = default;
    [[nodiscard]] virtual std::vector<TimedMidiNote> process(
        std::span<const TimedMidiNote> notes,
        const ScaleDefinition& scale) const = 0;
};

} // namespace hum::core
