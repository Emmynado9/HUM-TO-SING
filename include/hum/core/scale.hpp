#pragma once

#include "hum/core/midi.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace hum::core {

enum class ScaleMode : std::uint8_t {
    major,
    natural_minor,
};

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

class NearestScaleProcessor final : public IScaleProcessor {
public:
    [[nodiscard]] std::vector<TimedMidiNote> process(
        std::span<const TimedMidiNote> notes,
        const ScaleDefinition& scale) const override;
};

[[nodiscard]] ScaleDefinition makeScaleDefinition(
    std::uint8_t root_pitch_class,
    ScaleMode mode);
[[nodiscard]] std::string_view scaleModeName(ScaleMode mode) noexcept;
[[nodiscard]] std::string_view pitchClassName(std::uint8_t pitch_class) noexcept;

} // namespace hum::core
