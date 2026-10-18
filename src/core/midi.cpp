#include "hum/core/midi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hum::core {

bool MidiNote::isValid() const noexcept {
    return pitch <= 127
        && velocity >= 1 && velocity <= 127
        && duration_ticks > 0
        && channel <= 15;
}

bool TimedMidiNote::isValid() const noexcept {
    return pitch <= 127
        && velocity >= 1 && velocity <= 127
        && std::isfinite(start_seconds) && start_seconds >= 0.0
        && std::isfinite(duration_seconds) && duration_seconds > 0.0
        && channel <= 15;
}

bool MidiSequence::isValid() const noexcept {
    constexpr std::uint16_t kMaximumMetricDivision = 0x7fff;
    return ticks_per_quarter > 0
        && ticks_per_quarter <= kMaximumMetricDivision
        && std::isfinite(tempo_bpm)
        && tempo_bpm > 0.0
        && std::all_of(notes.cbegin(), notes.cend(), [](const MidiNote& note) {
            return note.isValid();
        }) && std::all_of(program_changes.cbegin(), program_changes.cend(), [](const auto& change) {
            return change.channel < 16 && change.program < 128;
        });
}

std::optional<double> MidiTimeline::ticksToSeconds(
    const std::uint64_t ticks,
    const double tempo_bpm,
    const std::uint16_t ticks_per_quarter) noexcept {
    if (!std::isfinite(tempo_bpm) || tempo_bpm <= 0.0 || ticks_per_quarter == 0) {
        return std::nullopt;
    }

    const auto seconds = static_cast<double>(ticks) * 60.0
        / (tempo_bpm * static_cast<double>(ticks_per_quarter));
    if (!std::isfinite(seconds)) {
        return std::nullopt;
    }
    return seconds;
}

std::optional<std::uint64_t> MidiTimeline::secondsToTicks(
    const double seconds,
    const double tempo_bpm,
    const std::uint16_t ticks_per_quarter) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0
        || !std::isfinite(tempo_bpm) || tempo_bpm <= 0.0
        || ticks_per_quarter == 0) {
        return std::nullopt;
    }

    const long double raw_ticks = static_cast<long double>(seconds)
        * static_cast<long double>(tempo_bpm)
        * static_cast<long double>(ticks_per_quarter) / 60.0L;
    const long double rounded_ticks = std::round(raw_ticks);
    if (rounded_ticks > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(rounded_ticks);
}

std::optional<MidiSequence> MidiTimeline::fromTimedNotes(
    const std::vector<TimedMidiNote>& notes,
    const double tempo_bpm,
    const std::uint16_t ticks_per_quarter) noexcept {
    MidiSequence sequence;
    sequence.tempo_bpm = tempo_bpm;
    sequence.ticks_per_quarter = ticks_per_quarter;
    if (!sequence.isValid()) {
        return std::nullopt;
    }

    sequence.notes.reserve(notes.size());
    for (const auto& note : notes) {
        if (!note.isValid()) {
            return std::nullopt;
        }
        if (note.muted) { continue; }
        const auto start = secondsToTicks(note.start_seconds, tempo_bpm, ticks_per_quarter);
        const auto end = secondsToTicks(
            note.start_seconds + note.duration_seconds, tempo_bpm, ticks_per_quarter);
        if (!start || !end || *start > std::numeric_limits<std::uint32_t>::max()
            || *end > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        const auto duration = std::max<std::uint64_t>(1, *end > *start ? *end - *start : 1);
        if (duration > std::numeric_limits<std::uint32_t>::max()
            || *start + duration > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        sequence.notes.push_back({
            note.pitch,
            note.velocity,
            static_cast<std::uint32_t>(*start),
            static_cast<std::uint32_t>(duration),
            note.channel,
        });
    }

    std::stable_sort(sequence.notes.begin(), sequence.notes.end(), [](const MidiNote& left, const MidiNote& right) {
        if (left.start_tick != right.start_tick) {
            return left.start_tick < right.start_tick;
        }
        return left.pitch < right.pitch;
    });
    return sequence.isValid() ? std::optional<MidiSequence>{std::move(sequence)} : std::nullopt;
}

bool Project::isValid() const noexcept {
    return schema_version > 0 && !name.empty() && sequence.isValid();
}

} // namespace hum::core
