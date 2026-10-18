#include "hum/core/midi_file.hpp"
#include "hum/core/note_extraction.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void expect(const bool condition, const std::string_view message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expectNear(
    const double actual,
    const double expected,
    const double tolerance,
    const std::string_view message) {
    expect(std::abs(actual - expected) <= tolerance, message);
}

[[nodiscard]] hum::core::PitchDetectorConfig pitchConfig() {
    hum::core::PitchDetectorConfig config;
    config.frame_size = 4'096;
    config.hop_size = 512;
    config.minimum_frequency_hz = 80.0;
    config.maximum_frequency_hz = 1'000.0;
    config.input_gate_rms = 0.01F;
    return config;
}

struct FixtureBuilder {
    explicit FixtureBuilder(const std::uint32_t rate = 48'000) : sample_rate(rate) {}

    void silence(const double seconds) {
        samples.insert(samples.end(), sampleCount(seconds), 0.0F);
    }

    void tone(
        const double frequency_hz,
        const double seconds,
        const float amplitude = 0.30F,
        const double vibrato_cents = 0.0) {
        const auto count = sampleCount(seconds);
        constexpr double ramp_seconds = 0.008;
        const auto ramp_samples = std::max<std::size_t>(1, sampleCount(ramp_seconds));
        for (std::size_t index = 0; index < count; ++index) {
            const auto time = static_cast<double>(index) / sample_rate;
            const auto cents = vibrato_cents
                * std::sin(2.0 * std::numbers::pi * 5.0 * time);
            const auto frequency = frequency_hz * std::pow(2.0, cents / 1'200.0);
            phase += 2.0 * std::numbers::pi * frequency / sample_rate;
            const auto attack = std::min(1.0, static_cast<double>(index + 1) / ramp_samples);
            const auto release = std::min(1.0, static_cast<double>(count - index) / ramp_samples);
            samples.push_back(amplitude * static_cast<float>(
                std::sin(phase) * std::min(attack, release)));
        }
    }

    [[nodiscard]] std::size_t sampleCount(const double seconds) const {
        return static_cast<std::size_t>(std::llround(seconds * sample_rate));
    }

    [[nodiscard]] hum::core::AudioRecording recording() && {
        return {{sample_rate, 1}, std::move(samples)};
    }

    std::uint32_t sample_rate;
    std::vector<float> samples;
    double phase{0.0};
};

[[nodiscard]] hum::core::PhraseConversionResult convert(
    const hum::core::AudioRecording& recording,
    const double tempo = 120.0,
    hum::core::PhraseConversionPipeline::ProgressCallback progress = {},
    hum::core::PhraseConversionPipeline::CancellationCheck cancelled = {}) {
    const hum::core::PhraseConversionPipeline pipeline(
        std::make_shared<hum::core::YinPitchDetector>(),
        std::make_shared<hum::core::HysteresisNoteSegmenter>());
    return pipeline.convert(
        recording, pitchConfig(), {}, tempo, 480, std::move(progress), std::move(cancelled));
}

void testKnownPhraseAndTiming() {
    FixtureBuilder fixture;
    fixture.silence(0.20);
    fixture.tone(440.0, 0.36);
    fixture.silence(0.18);
    fixture.tone(523.251131, 0.30);
    fixture.silence(0.18);
    fixture.tone(523.251131, 0.27);
    fixture.silence(0.20);
    const auto recording = std::move(fixture).recording();

    double progress = 0.0;
    const auto conversion_started = std::chrono::steady_clock::now();
    const auto result = convert(recording, 120.0, [&progress](const double value) {
        progress = value;
    });
    const auto conversion_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - conversion_started).count();
    expect(result.status == hum::core::PhraseConversionStatus::success,
        "known phrase conversion succeeds");
    expect(!result.raw_pitch_frames.empty(), "raw analysis remains available separately");
    expect(result.notes.size() == 3, "pause-separated phrase creates three notes");
    expect(progress == 1.0, "successful conversion reports complete progress");

    const std::vector<int> pitches{69, 72, 72};
    const std::vector<double> starts{0.20, 0.74, 1.22};
    const std::vector<double> durations{0.36, 0.30, 0.27};
    const auto tolerance = static_cast<double>(pitchConfig().frame_size)
            / static_cast<double>(recording.format().sample_rate_hz)
        + 2.0 * static_cast<double>(pitchConfig().hop_size)
            / static_cast<double>(recording.format().sample_rate_hz);
    double maximum_onset_error = 0.0;
    double maximum_offset_error = 0.0;
    for (std::size_t index = 0; index < result.notes.size() && index < pitches.size(); ++index) {
        const auto& note = result.notes[index];
        expect(note.pitch == pitches[index], "known phrase pitch matches fixture");
        expect(note.velocity == 96, "milestone uses the documented fixed velocity");
        expect(note.duration_seconds > 0.0, "extracted duration is positive");
        expect(note.start_seconds >= 0.0
                && note.start_seconds + note.duration_seconds <= recording.durationSeconds() + 1.0e-9,
            "extracted note stays inside phrase bounds");
        if (index > 0) {
            expect(result.notes[index - 1].start_seconds <= note.start_seconds,
                "events are ordered by start time");
        }
        const auto onset_error = std::abs(note.start_seconds - starts[index]);
        const auto expected_end = starts[index] + durations[index];
        const auto actual_end = note.start_seconds + note.duration_seconds;
        const auto offset_error = std::abs(actual_end - expected_end);
        maximum_onset_error = std::max(maximum_onset_error, onset_error);
        maximum_offset_error = std::max(maximum_offset_error, offset_error);
        expect(onset_error <= tolerance, "note onset is inside window-plus-two-hops tolerance");
        expect(offset_error <= tolerance, "note offset is inside window-plus-two-hops tolerance");
    }
    std::cout << "known-phrase max onset error: " << maximum_onset_error * 1'000.0
              << " ms; max offset error: " << maximum_offset_error * 1'000.0
              << " ms; tolerance: " << tolerance * 1'000.0
              << " ms; " << recording.durationSeconds() << " s processed in "
              << conversion_ms << " ms\n";
}

void testSilenceShortNotesAndVibrato() {
    FixtureBuilder silent;
    silent.silence(0.5);
    const auto silence_result = convert(std::move(silent).recording());
    expect(silence_result.status == hum::core::PhraseConversionStatus::empty,
        "silence reports an empty conversion");
    expect(silence_result.notes.empty(), "silence does not create notes");

    FixtureBuilder short_tone;
    short_tone.silence(0.20);
    short_tone.tone(440.0, 0.015);
    short_tone.silence(0.20);
    const auto short_result = convert(std::move(short_tone).recording());
    expect(short_result.notes.empty(), "a tone below minimum note duration is omitted");

    FixtureBuilder vibrato;
    vibrato.silence(0.20);
    vibrato.tone(440.0, 0.60, 0.30F, 45.0);
    vibrato.silence(0.20);
    const auto vibrato_result = convert(std::move(vibrato).recording());
    expect(vibrato_result.notes.size() == 1, "45-cent vibrato stays one note");
    expect(vibrato_result.notes.size() == 1 && vibrato_result.notes.front().pitch == 69,
        "vibrato retains the center pitch");
}

[[nodiscard]] hum::core::RawPitchEstimate rawFrame(
    const std::uint64_t start,
    const double frequency,
    const bool reliable = true) {
    return {
        reliable ? hum::core::PitchEstimateStatus::reliable
                 : hum::core::PitchEstimateStatus::below_gate,
        start,
        reliable ? frequency : 0.0,
        reliable ? 0.98 : 0.0,
        reliable ? 0.20 : 0.0,
    };
}

void testGapHysteresisAndRepeatedNotes() {
    hum::core::NoteExtractionConfig config;
    config.minimum_note_duration_seconds = 0.025;
    config.maximum_unreliable_gap_seconds = 0.025;
    // This fixture explicitly exercises the fast, frame-count configuration.
    config.transition_confirmation_seconds = 0.020;
    hum::core::HysteresisNoteSegmenter segmenter;

    std::vector<hum::core::RawPitchEstimate> short_gap;
    for (std::uint64_t start = 0; start < 80; start += 10) {
        short_gap.push_back(rawFrame(start, 440.0, start != 30 && start != 40));
    }
    const auto bridged = segmenter.segment(short_gap, 1'000, 40, 10, 0.12, 440.0, config);
    expect(bridged.size() == 1, "a short uncertain gap is bridged");

    std::vector<hum::core::RawPitchEstimate> long_pause;
    for (std::uint64_t start = 0; start < 160; start += 10) {
        const auto voiced = start < 50 || start >= 110;
        long_pause.push_back(rawFrame(start, 440.0, voiced));
    }
    const auto repeated = segmenter.segment(long_pause, 1'000, 40, 10, 0.20, 440.0, config);
    expect(repeated.size() == 2, "a clear pause splits repeated same-pitch notes");
    expect(std::all_of(repeated.cbegin(), repeated.cend(), [](const auto& note) {
        return note.pitch == 69;
    }), "pause-separated repeated notes retain pitch");

    std::vector<hum::core::RawPitchEstimate> uninterrupted;
    for (std::uint64_t start = 0; start < 160; start += 10) {
        uninterrupted.push_back(rawFrame(start, 440.0));
    }
    const auto one_note = segmenter.segment(uninterrupted, 1'000, 40, 10, 0.20, 440.0, config);
    expect(one_note.size() == 1, "uninterrupted same pitch remains one note");

    std::vector<hum::core::RawPitchEstimate> fluctuation;
    for (std::uint64_t start = 0; start < 160; start += 10) {
        const auto cents = start < 30 ? 0.0 : (start % 20 == 0 ? 55.0 : -55.0);
        fluctuation.push_back(rawFrame(start, 440.0 * std::pow(2.0, cents / 1'200.0)));
    }
    const auto stable = segmenter.segment(fluctuation, 1'000, 40, 10, 0.20, 440.0, config);
    expect(stable.size() == 1, "pitch-transition hysteresis prevents fluctuation splits");

    std::vector<hum::core::RawPitchEstimate> transition_frames;
    for (std::uint64_t start = 0; start < 160; start += 10) {
        transition_frames.push_back(rawFrame(
            start, start < 80 ? 440.0 : 523.251131));
    }
    const auto transitioned = segmenter.segment(
        transition_frames, 1'000, 40, 10, 0.20, 440.0, config);
    expect(transitioned.size() == 2, "a sustained pitch transition creates a new note");
    expect(transitioned.size() == 2
            && transitioned[0].pitch == 69 && transitioned[1].pitch == 72,
        "transition confirmation retains both expected pitches");
}

void testTempoAndTickConversion() {
    const std::vector<hum::core::TimedMidiNote> notes{{69, 96, 0.5, 0.25, 0}};
    for (const auto& expected : std::vector<std::pair<double, std::pair<std::uint32_t, std::uint32_t>>>{
             {60.0, {240, 120}}, {120.0, {480, 240}}, {180.0, {720, 360}}}) {
        const auto sequence = hum::core::MidiTimeline::fromTimedNotes(notes, expected.first, 480);
        expect(sequence.has_value(), "seconds convert at a supported tempo");
        expect(sequence && sequence->notes.front().start_tick == expected.second.first,
            "note onset converts to expected ticks");
        expect(sequence && sequence->notes.front().duration_ticks == expected.second.second,
            "note duration converts to expected ticks");
        const auto restored = sequence
            ? hum::core::MidiTimeline::ticksToSeconds(
                sequence->notes.front().start_tick, expected.first, 480)
            : std::nullopt;
        expect(restored && std::abs(*restored - 0.5) < 1.0e-9,
            "tempo-dependent ticks retain original seconds");
    }
}

struct ParsedMidi {
    std::uint16_t format{0xffff};
    std::uint16_t tracks{0};
    std::uint16_t division{0};
    std::uint32_t tempo{0};
    struct Event {
        std::uint32_t tick;
        bool on;
        std::uint8_t pitch;
        std::uint8_t velocity;
    };
    std::vector<Event> events;
};

[[nodiscard]] std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    const auto value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes.at(cursor)) << 8U) | bytes.at(cursor + 1));
    cursor += 2;
    return value;
}

[[nodiscard]] std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value = (value << 8U) | bytes.at(cursor++);
    }
    return value;
}

[[nodiscard]] std::uint32_t readVariable(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    std::uint32_t value = 0;
    for (int count = 0; count < 4; ++count) {
        const auto byte = bytes.at(cursor++);
        value = (value << 7U) | (byte & 0x7fU);
        if ((byte & 0x80U) == 0) {
            return value;
        }
    }
    throw std::runtime_error("invalid variable-length MIDI value");
}

[[nodiscard]] ParsedMidi parseMidi(const std::vector<std::uint8_t>& bytes) {
    ParsedMidi result;
    std::size_t cursor = 0;
    expect(bytes.size() >= 22, "MIDI reader receives complete header and track chunks");
    expect(std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "MThd",
        "MIDI header chunk is present");
    cursor = 4;
    expect(readU32(bytes, cursor) == 6, "MIDI header has the standard six-byte payload");
    result.format = readU16(bytes, cursor);
    result.tracks = readU16(bytes, cursor);
    result.division = readU16(bytes, cursor);
    expect(std::string_view(reinterpret_cast<const char*>(bytes.data() + cursor), 4) == "MTrk",
        "MIDI track chunk is present");
    cursor += 4;
    const auto track_length = readU32(bytes, cursor);
    const auto track_end = cursor + track_length;
    std::uint32_t tick = 0;
    while (cursor < track_end) {
        tick += readVariable(bytes, cursor);
        const auto status = bytes.at(cursor++);
        if (status == 0xffU) {
            const auto type = bytes.at(cursor++);
            const auto length = readVariable(bytes, cursor);
            if (type == 0x51U && length == 3) {
                result.tempo = (static_cast<std::uint32_t>(bytes.at(cursor)) << 16U)
                    | (static_cast<std::uint32_t>(bytes.at(cursor + 1)) << 8U)
                    | bytes.at(cursor + 2);
            }
            cursor += length;
            continue;
        }
        const auto pitch = bytes.at(cursor++);
        const auto velocity = bytes.at(cursor++);
        const auto kind = status & 0xf0U;
        expect(kind == 0x80U || kind == 0x90U, "track contains only expected channel note events");
        result.events.push_back({tick, kind == 0x90U && velocity > 0, pitch, velocity});
    }
    expect(cursor == track_end, "MIDI parser consumes exactly the track chunk");
    return result;
}

void testMidiFileWithIndependentReader() {
    const std::vector<hum::core::TimedMidiNote> notes{
        {60, 96, 0.0, 0.5, 0},
        {60, 96, 0.5, 0.25, 0},
        {64, 96, 0.75, 0.5, 0},
    };
    const auto sequence = hum::core::MidiTimeline::fromTimedNotes(notes, 120.0, 480);
    expect(sequence.has_value(), "test sequence converts to MIDI ticks");
    const auto bytes = sequence
        ? hum::core::StandardMidiFileExporter::serialize(*sequence) : std::nullopt;
    expect(bytes.has_value(), "valid sequence serializes as a Standard MIDI File");
    if (!bytes) {
        return;
    }
    const auto parsed = parseMidi(*bytes);
    expect(parsed.format == 0, "export uses Standard MIDI File format 0");
    expect(parsed.tracks == 1, "format-0 export contains one track");
    expect(parsed.division == 480, "export writes ticks per quarter note");
    expect(parsed.tempo == 500'000, "120 BPM is encoded as 500000 microseconds per quarter");
    expect(parsed.events.size() == notes.size() * 2, "every note has one note-on and one note-off");
    for (const auto& note : sequence->notes) {
        const auto on = std::find_if(parsed.events.cbegin(), parsed.events.cend(), [&note](const auto& event) {
            return event.on && event.pitch == note.pitch && event.tick == note.start_tick;
        });
        const auto off_tick = note.start_tick + note.duration_ticks;
        const auto off = std::find_if(parsed.events.cbegin(), parsed.events.cend(), [&note, off_tick](const auto& event) {
            return !event.on && event.pitch == note.pitch && event.tick == off_tick;
        });
        expect(on != parsed.events.cend(), "independent reader finds expected note-on");
        expect(off != parsed.events.cend(), "independent reader finds paired note-off at expected duration");
    }
    const auto repeated_boundary = std::find_if(parsed.events.cbegin(), parsed.events.cend(), [](const auto& event) {
        return event.tick == 480 && !event.on && event.pitch == 60;
    });
    expect(repeated_boundary != parsed.events.cend()
            && std::next(repeated_boundary) != parsed.events.cend()
            && std::next(repeated_boundary)->tick == 480
            && std::next(repeated_boundary)->on,
        "note-off precedes repeated note-on at the same tick");

    const auto path = std::filesystem::temp_directory_path() / "hum-note-extraction-test.mid";
    hum::core::StandardMidiFileExporter exporter;
    expect(exporter.exportFile(*sequence, path) == hum::core::ServiceStatus::ok,
        "MIDI exporter writes a local file");
    std::ifstream input(path, std::ios::binary);
    const std::vector<std::uint8_t> disk_bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    expect(disk_bytes == *bytes, "exported file matches the validated serialized bytes");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    expect(!std::filesystem::exists(path), "test export is cleaned up");
}

void testCancellation() {
    FixtureBuilder fixture;
    fixture.tone(440.0, 0.5);
    const auto result = convert(
        std::move(fixture).recording(), 120.0, {}, [] { return true; });
    expect(result.status == hum::core::PhraseConversionStatus::cancelled,
        "conversion honors cancellation before analysis");
    expect(result.notes.empty(), "cancelled conversion emits no derived notes");
}

} // namespace

int main() {
    testKnownPhraseAndTiming();
    testSilenceShortNotesAndVibrato();
    testGapHysteresisAndRepeatedNotes();
    testTempoAndTickConversion();
    testMidiFileWithIndependentReader();
    testCancellation();

    if (failures == 0) {
        std::cout << "All note extraction tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " note extraction test assertion(s) failed.\n";
    return 1;
}
