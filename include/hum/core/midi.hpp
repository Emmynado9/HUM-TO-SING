#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hum::core {

inline constexpr std::uint16_t kDefaultTicksPerQuarter = 480;
inline constexpr double kDefaultTempoBpm = 120.0;

struct MidiNote {
    std::uint8_t pitch{60};
    std::uint8_t velocity{100};
    std::uint32_t start_tick{0};
    std::uint32_t duration_ticks{kDefaultTicksPerQuarter};
    std::uint8_t channel{0};

    [[nodiscard]] bool isValid() const noexcept;
};

// The analysis-time note model. Times are seconds from the first captured
// audio frame; conversion to ticks happens only when a MIDI sequence is made.
struct TimedMidiNote {
    std::uint8_t pitch{60};
    std::uint8_t velocity{96};
    double start_seconds{0.0};
    double duration_seconds{0.5};
    std::uint8_t channel{0};

    [[nodiscard]] bool isValid() const noexcept;
};

struct MidiSequence {
    std::uint16_t ticks_per_quarter{kDefaultTicksPerQuarter};
    double tempo_bpm{kDefaultTempoBpm};
    std::vector<MidiNote> notes;

    [[nodiscard]] bool isValid() const noexcept;
};

class MidiTimeline final {
public:
    [[nodiscard]] static std::optional<double> ticksToSeconds(
        std::uint64_t ticks,
        double tempo_bpm,
        std::uint16_t ticks_per_quarter) noexcept;

    [[nodiscard]] static std::optional<std::uint64_t> secondsToTicks(
        double seconds,
        double tempo_bpm,
        std::uint16_t ticks_per_quarter) noexcept;

    [[nodiscard]] static std::optional<MidiSequence> fromTimedNotes(
        const std::vector<TimedMidiNote>& notes,
        double tempo_bpm = kDefaultTempoBpm,
        std::uint16_t ticks_per_quarter = kDefaultTicksPerQuarter) noexcept;
};

struct Project {
    std::uint32_t schema_version{1};
    std::string name{"Untitled sketch"};
    MidiSequence sequence;

    [[nodiscard]] bool isValid() const noexcept;
};

} // namespace hum::core
