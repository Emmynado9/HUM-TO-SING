#include "hum/core/midi_file.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <utility>

namespace hum::core {

namespace {

constexpr std::uint32_t kMaximumVariableLengthValue = 0x0fff'ffffU;

struct MidiEvent {
    std::uint32_t tick{0};
    bool note_on{false};
    std::uint8_t pitch{0};
    std::uint8_t velocity{0};
    std::uint8_t channel{0};
    std::optional<std::uint8_t> program;
};

void appendU16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

[[nodiscard]] bool appendVariableLength(
    std::vector<std::uint8_t>& bytes,
    const std::uint32_t value) {
    if (value > kMaximumVariableLengthValue) {
        return false;
    }
    std::uint32_t buffer = value & 0x7fU;
    for (auto remaining = value >> 7U; remaining != 0; remaining >>= 7U) {
        buffer <<= 8U;
        buffer |= 0x80U | (remaining & 0x7fU);
    }
    for (;;) {
        bytes.push_back(static_cast<std::uint8_t>(buffer & 0xffU));
        if ((buffer & 0x80U) == 0) {
            break;
        }
        buffer >>= 8U;
    }
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>> StandardMidiFileExporter::serialize(
    const MidiSequence& sequence) noexcept {
    try {
        if (!sequence.isValid()) {
            return std::nullopt;
        }
        const auto microseconds_per_quarter = std::llround(60'000'000.0 / sequence.tempo_bpm);
        if (microseconds_per_quarter < 1 || microseconds_per_quarter > 0x00ff'ffff) {
            return std::nullopt;
        }

        std::vector<MidiEvent> events;
        events.reserve(sequence.notes.size() * 2);
        for (const auto& note : sequence.notes) {
            if (!note.isValid()
                || note.duration_ticks > std::numeric_limits<std::uint32_t>::max() - note.start_tick) {
                return std::nullopt;
            }
            events.push_back({note.start_tick, true, note.pitch, note.velocity, note.channel});
            events.push_back({
                static_cast<std::uint32_t>(note.start_tick + note.duration_ticks),
                false,
                note.pitch,
                0,
                note.channel,
            });
        }
        for (const auto& change : sequence.program_changes) {
            if (change.channel > 15 || change.program > 127) { return std::nullopt; }
            events.push_back({0, false, 0, 0, change.channel, change.program});
        }
        std::stable_sort(events.begin(), events.end(), [](const MidiEvent& left, const MidiEvent& right) {
            if (left.tick != right.tick) {
                return left.tick < right.tick;
            }
            if (left.program.has_value() != right.program.has_value()) {
                return left.program.has_value();
            }
            if (left.note_on != right.note_on) {
                return !left.note_on;
            }
            if (left.channel != right.channel) {
                return left.channel < right.channel;
            }
            return left.pitch < right.pitch;
        });

        std::vector<std::uint8_t> track;
        track.reserve(16 + events.size() * 5);
        static_cast<void>(appendVariableLength(track, 0));
        track.insert(track.end(), {0xffU, 0x51U, 0x03U});
        track.push_back(static_cast<std::uint8_t>((microseconds_per_quarter >> 16) & 0xff));
        track.push_back(static_cast<std::uint8_t>((microseconds_per_quarter >> 8) & 0xff));
        track.push_back(static_cast<std::uint8_t>(microseconds_per_quarter & 0xff));

        std::uint32_t previous_tick = 0;
        for (const auto& event : events) {
            const auto delta = event.tick - previous_tick;
            if (!appendVariableLength(track, delta)) {
                return std::nullopt;
            }
            if (event.program) {
                track.push_back(static_cast<std::uint8_t>(0xc0U | event.channel));
                track.push_back(*event.program);
            } else {
                track.push_back(static_cast<std::uint8_t>(
                    (event.note_on ? 0x90U : 0x80U) | event.channel));
                track.push_back(event.pitch);
                track.push_back(event.velocity);
            }
            previous_tick = event.tick;
        }
        static_cast<void>(appendVariableLength(track, 0));
        track.insert(track.end(), {0xffU, 0x2fU, 0x00U});
        if (track.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }

        std::vector<std::uint8_t> file;
        file.reserve(14 + 8 + track.size());
        file.insert(file.end(), {'M', 'T', 'h', 'd'});
        appendU32(file, 6);
        appendU16(file, 0);
        appendU16(file, 1);
        appendU16(file, sequence.ticks_per_quarter);
        file.insert(file.end(), {'M', 'T', 'r', 'k'});
        appendU32(file, static_cast<std::uint32_t>(track.size()));
        file.insert(file.end(), track.begin(), track.end());
        return file;
    } catch (...) {
        return std::nullopt;
    }
}

ServiceStatus StandardMidiFileExporter::exportFile(
    const MidiSequence& sequence,
    const std::filesystem::path& destination) {
    if (destination.empty()) {
        return ServiceStatus::invalid_input;
    }
    const auto bytes = serialize(sequence);
    if (!bytes) {
        return ServiceStatus::invalid_input;
    }
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        return ServiceStatus::io_error;
    }
    output.write(
        reinterpret_cast<const char*>(bytes->data()),
        static_cast<std::streamsize>(bytes->size()));
    output.close();
    return output ? ServiceStatus::ok : ServiceStatus::io_error;
}

} // namespace hum::core
