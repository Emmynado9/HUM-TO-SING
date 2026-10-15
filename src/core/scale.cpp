#include "hum/core/scale.hpp"

#include <algorithm>
#include <array>

namespace hum::core {

bool ScaleDefinition::isValid() const noexcept {
    if (root_pitch_class > 11 || intervals.empty()) {
        return false;
    }

    std::array<bool, 12> seen{};
    for (const auto interval : intervals) {
        if (interval > 11 || seen[interval]) {
            return false;
        }
        seen[interval] = true;
    }
    return true;
}

bool ScaleDefinition::contains(const std::uint8_t midi_pitch) const noexcept {
    if (!isValid() || midi_pitch > 127) {
        return false;
    }

    const auto pitch_class = static_cast<std::uint8_t>(midi_pitch % 12);
    const auto interval = static_cast<std::uint8_t>(
        (static_cast<unsigned int>(pitch_class) + 12U - root_pitch_class) % 12U);
    return std::find(intervals.cbegin(), intervals.cend(), interval) != intervals.cend();
}

std::uint8_t ScaleDefinition::nearestPitch(const std::uint8_t midi_pitch) const noexcept {
    if (!isValid()) {
        return midi_pitch;
    }

    const int bounded_pitch = std::min(static_cast<int>(midi_pitch), 127);
    if (contains(static_cast<std::uint8_t>(bounded_pitch))) {
        return static_cast<std::uint8_t>(bounded_pitch);
    }

    for (int distance = 1; distance <= 127; ++distance) {
        // Checking the lower candidate first defines the equal-distance tie break.
        const int lower = bounded_pitch - distance;
        if (lower >= 0 && contains(static_cast<std::uint8_t>(lower))) {
            return static_cast<std::uint8_t>(lower);
        }

        const int upper = bounded_pitch + distance;
        if (upper <= 127 && contains(static_cast<std::uint8_t>(upper))) {
            return static_cast<std::uint8_t>(upper);
        }
    }

    return static_cast<std::uint8_t>(bounded_pitch);
}

std::vector<TimedMidiNote> NearestScaleProcessor::process(
    const std::span<const TimedMidiNote> notes,
    const ScaleDefinition& scale) const {
    std::vector<TimedMidiNote> result(notes.begin(), notes.end());
    if (!scale.isValid()) {
        return result;
    }
    for (auto& note : result) {
        note.pitch = scale.nearestPitch(note.pitch);
    }
    return result;
}

ScaleDefinition makeScaleDefinition(
    const std::uint8_t root_pitch_class,
    const ScaleMode mode) {
    switch (mode) {
    case ScaleMode::major:
        return {root_pitch_class, {0, 2, 4, 5, 7, 9, 11}};
    case ScaleMode::natural_minor:
        return {root_pitch_class, {0, 2, 3, 5, 7, 8, 10}};
    }
    return {root_pitch_class, {}};
}

std::string_view scaleModeName(const ScaleMode mode) noexcept {
    switch (mode) {
    case ScaleMode::major:
        return "Major";
    case ScaleMode::natural_minor:
        return "Natural minor";
    }
    return "Unknown scale";
}

std::string_view pitchClassName(const std::uint8_t pitch_class) noexcept {
    constexpr std::array<std::string_view, 12> names{
        "C", "C#/Db", "D", "D#/Eb", "E", "F",
        "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
    return pitch_class < names.size() ? names[pitch_class] : std::string_view{"?"};
}

} // namespace hum::core
