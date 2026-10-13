#include "hum/core/scale.hpp"

#include <array>
#include <algorithm>

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

} // namespace hum::core
