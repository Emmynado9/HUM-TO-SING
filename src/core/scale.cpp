#include "hum/core/scale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>

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

std::vector<TimedMidiNote> makeDiatonicTriads(
    const std::span<const TimedMidiNote> roots, const ScaleDefinition& scale, const bool connect_roots) {
    std::vector<TimedMidiNote> result;
    if (!scale.isValid() || scale.intervals.size() != 7 || roots.size() > 33'333) { return result; }
    std::vector<TimedMidiNote> prepared(roots.begin(), roots.end());
    for (auto& root : prepared) {
        if (!root.isValid()) { return {}; }
        root.pitch = scale.nearestPitch(root.pitch);
    }
    if (connect_roots) {
        std::stable_sort(prepared.begin(), prepared.end(), [](const auto& a, const auto& b) {
            return a.start_seconds < b.start_seconds;
        });
        std::vector<TimedMidiNote> joined;
        for (std::size_t i = 0; i < prepared.size(); ++i) {
            auto root = prepared[i];
            if (i + 1 < prepared.size()) {
                root.duration_seconds = prepared[i + 1].start_seconds - root.start_seconds;
                if (root.duration_seconds <= 0.0) { continue; }
            }
            if (!joined.empty() && joined.back().pitch == root.pitch && joined.back().muted == root.muted) {
                joined.back().duration_seconds = root.start_seconds + root.duration_seconds - joined.back().start_seconds;
            } else { joined.push_back(root); }
        }
        prepared = std::move(joined);
    }
    result.reserve(roots.size() * 3);
    for (const auto& source : prepared) {
        if (!source.isValid()) { return {}; }
        auto root = source;
        root.pitch = scale.nearestPitch(source.pitch);
        const auto relative = static_cast<std::uint8_t>((root.pitch % 12 + 12 - scale.root_pitch_class) % 12);
        const auto it = std::find(scale.intervals.begin(), scale.intervals.end(), relative);
        if (it == scale.intervals.end()) { return {}; }
        const auto degree = static_cast<std::size_t>(std::distance(scale.intervals.begin(), it));
        // Keep all three voices at the top of the MIDI range by lowering the whole voicing.
        if (root.pitch > 119) { root.pitch = static_cast<std::uint8_t>(root.pitch - 12); }
        result.push_back(root);
        for (const auto steps : {2U, 4U}) {
            const auto target = scale.intervals[(degree + steps) % scale.intervals.size()];
            auto pitch = static_cast<int>(root.pitch) + static_cast<int>(target) - static_cast<int>(relative);
            if (pitch <= root.pitch) { pitch += 12; }
            if (pitch <= 127) { auto voice = root; voice.pitch = static_cast<std::uint8_t>(pitch); result.push_back(voice); }
        }
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> parseChordDegrees(const std::string_view text) {
    if (text.size() > 4096) { return std::nullopt; }
    constexpr std::array<std::string_view, 7> romans{"I","II","III","IV","V","VI","VII"};
    std::vector<std::uint8_t> degrees;
    std::string token;
    auto append = [&]() {
        if (token.empty()) { return true; }
        auto found = std::find(romans.begin(), romans.end(), token);
        if (found != romans.end()) { degrees.push_back(static_cast<std::uint8_t>(found - romans.begin())); }
        else if (token.size() == 1 && token[0] >= '1' && token[0] <= '7') {
            degrees.push_back(static_cast<std::uint8_t>(token[0] - '1'));
        } else { return false; }
        token.clear();
        return degrees.size() <= 128;
    };
    for (const char value : text) {
        const auto character = static_cast<unsigned char>(value);
        if (std::isspace(character) || character == '-' || character == ',' || character == '|') {
            if (!append()) { return std::nullopt; }
        } else { token.push_back(static_cast<char>(std::toupper(character))); }
    }
    if (!append() || degrees.empty()) { return std::nullopt; }
    return degrees;
}

std::string chordDegreeName(const ScaleDefinition& scale, const std::uint8_t degree) {
    if (!scale.isValid() || scale.intervals.size() != 7 || degree >= 7) { return {}; }
    constexpr std::array<std::string_view, 7> romans{"I","II","III","IV","V","VI","VII"};
    const int third = (scale.intervals[(degree + 2) % 7] + 12 - scale.intervals[degree]) % 12;
    const int fifth = (scale.intervals[(degree + 4) % 7] + 12 - scale.intervals[degree]) % 12;
    std::string roman(romans[degree]);
    if (third == 3) { for (auto& c : roman) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); } }
    std::string quality = fifth == 6 ? " dim" : third == 3 ? " minor" : " major";
    if (fifth == 6) { roman += "°"; }
    return roman + " · " + std::string(pitchClassName(
        static_cast<std::uint8_t>((scale.root_pitch_class + scale.intervals[degree]) % 12))) + quality;
}

std::vector<TimedMidiNote> makeChordProgression(const std::span<const std::uint8_t> degrees,
    const ScaleDefinition& scale, const double seconds_per_chord) {
    if (!scale.isValid() || scale.intervals.size() != 7 || degrees.empty() || degrees.size() > 128
        || !std::isfinite(seconds_per_chord) || seconds_per_chord <= 0
        || seconds_per_chord * degrees.size() > 600) { return {}; }
    std::vector<TimedMidiNote> roots;
    for (std::size_t i = 0; i < degrees.size(); ++i) {
        if (degrees[i] >= 7) { return {}; }
        roots.push_back({static_cast<std::uint8_t>(48 + scale.root_pitch_class + scale.intervals[degrees[i]]),
            90, i * seconds_per_chord, seconds_per_chord, 0});
    }
    return makeDiatonicTriads(roots, scale);
}

} // namespace hum::core
