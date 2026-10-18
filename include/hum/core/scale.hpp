#pragma once
#include <optional>
#include <string>

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
// Turns each hummed scale degree into its root-position diatonic triad.
[[nodiscard]] std::vector<TimedMidiNote> makeDiatonicTriads(
    std::span<const TimedMidiNote> roots, const ScaleDefinition& scale, bool connect_roots = false);
// Degrees are zero-based, independent of key. Numeral case does not override diatonic quality.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> parseChordDegrees(std::string_view text);
[[nodiscard]] std::string chordDegreeName(const ScaleDefinition& scale, std::uint8_t degree);
[[nodiscard]] std::vector<TimedMidiNote> makeChordProgression(
    std::span<const std::uint8_t> degrees, const ScaleDefinition& scale, double seconds_per_chord);

} // namespace hum::core
