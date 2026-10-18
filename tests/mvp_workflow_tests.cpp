#include "support/FakeAudioCapture.hpp"

#include "hum/core/midi_file.hpp"
#include "hum/core/note_editor.hpp"
#include "hum/core/note_extraction.hpp"
#include "hum/core/project_file.hpp"
#include "hum/core/recording.hpp"
#include "hum/core/scale.hpp"
#include "hum/core/simple_synth.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

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

struct PhraseFixture {
    void silence(const double seconds) {
        samples.insert(samples.end(), count(seconds), 0.0F);
    }

    void tone(const double frequency_hz, const double seconds) {
        const auto frames = count(seconds);
        const auto ramp = count(0.008);
        for (std::size_t index = 0; index < frames; ++index) {
            phase += 2.0 * std::numbers::pi * frequency_hz / sample_rate;
            const auto attack = std::min(1.0,
                static_cast<double>(index + 1) / static_cast<double>(ramp));
            const auto release = std::min(1.0,
                static_cast<double>(frames - index) / static_cast<double>(ramp));
            samples.push_back(0.3F * static_cast<float>(
                std::sin(phase) * std::min(attack, release)));
        }
    }

    [[nodiscard]] std::size_t count(const double seconds) const {
        return static_cast<std::size_t>(std::llround(seconds * sample_rate));
    }

    std::uint32_t sample_rate{48'000};
    std::vector<float> samples;
    double phase{0.0};
};

[[nodiscard]] std::uint16_t readU16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& cursor) {
    const auto value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes.at(cursor)) << 8U) | bytes.at(cursor + 1));
    cursor += 2;
    return value;
}

[[nodiscard]] std::uint32_t readU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& cursor) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value = (value << 8U) | bytes.at(cursor++);
    }
    return value;
}

[[nodiscard]] std::uint32_t readVariable(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& cursor) {
    std::uint32_t value = 0;
    for (int count = 0; count < 4; ++count) {
        const auto byte = bytes.at(cursor++);
        value = (value << 7U) | (byte & 0x7fU);
        if ((byte & 0x80U) == 0) {
            return value;
        }
    }
    throw std::runtime_error("invalid MIDI variable-length value");
}

struct ParsedMidi {
    std::uint16_t format{0};
    std::uint16_t tracks{0};
    std::uint16_t division{0};
    std::uint32_t tempo_microseconds{0};
    std::vector<std::uint8_t> note_on_pitches;
    std::size_t note_off_count{0};
};

[[nodiscard]] ParsedMidi independentlyParseMidi(const std::vector<std::uint8_t>& bytes) {
    ParsedMidi parsed;
    std::size_t cursor = 4;
    static_cast<void>(readU32(bytes, cursor));
    parsed.format = readU16(bytes, cursor);
    parsed.tracks = readU16(bytes, cursor);
    parsed.division = readU16(bytes, cursor);
    cursor += 4;
    const auto track_length = readU32(bytes, cursor);
    const auto track_end = cursor + track_length;
    while (cursor < track_end) {
        static_cast<void>(readVariable(bytes, cursor));
        const auto status = bytes.at(cursor++);
        if (status == 0xffU) {
            const auto type = bytes.at(cursor++);
            const auto length = readVariable(bytes, cursor);
            if (type == 0x51U && length == 3) {
                parsed.tempo_microseconds =
                    (static_cast<std::uint32_t>(bytes.at(cursor)) << 16U)
                    | (static_cast<std::uint32_t>(bytes.at(cursor + 1)) << 8U)
                    | bytes.at(cursor + 2);
            }
            cursor += length;
            continue;
        }
        const auto pitch = bytes.at(cursor++);
        const auto velocity = bytes.at(cursor++);
        if ((status & 0xf0U) == 0x90U && velocity > 0) {
            parsed.note_on_pitches.push_back(pitch);
        } else if ((status & 0xf0U) == 0x80U) {
            ++parsed.note_off_count;
        }
    }
    return parsed;
}

void testCompleteFakeHardwareWorkflow() {
    PhraseFixture fixture;
    fixture.silence(0.20);
    fixture.tone(277.182631, 0.34); // C#4: deliberately outside C major.
    fixture.silence(0.16);
    fixture.tone(329.627557, 0.32); // E4.
    fixture.silence(0.16);
    fixture.tone(391.995436, 0.30); // G4.
    fixture.silence(0.20);
    const auto phrase_seconds = static_cast<double>(fixture.samples.size()) / fixture.sample_rate;

    hum::test::FakeAudioCapture source;
    source.device_format = {fixture.sample_rate, 1};
    hum::core::RecordingController recorder(source, 5.0);
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "workflow launches a fake local microphone session");
    source.emit(fixture.samples);
    expect(recorder.stop(), "workflow stops the fake recording");
    const auto recording = recorder.recording();
    expect(recording && recording->sampleCount() == fixture.samples.size(),
        "capture metadata retains the full local phrase");

    hum::core::PitchDetectorConfig pitch_config;
    pitch_config.frame_size = 4'096;
    pitch_config.hop_size = 512;
    pitch_config.input_gate_rms = 0.01F;
    pitch_config.minimum_frequency_hz = 80.0;
    pitch_config.maximum_frequency_hz = 1'000.0;
    const hum::core::PhraseConversionPipeline pipeline(
        std::make_shared<hum::core::YinPitchDetector>(),
        std::make_shared<hum::core::HysteresisNoteSegmenter>());
    const auto conversion_start = std::chrono::steady_clock::now();
    const auto conversion = pipeline.convert(
        *recording, pitch_config, {}, 96.0, 480);
    const auto conversion_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - conversion_start).count();
    expect(conversion.status == hum::core::PhraseConversionStatus::success,
        "recorded phrase converts successfully after Stop");
    expect(conversion.notes.size() == 3, "known phrase produces three inspectable notes");

    const auto scale = hum::core::makeScaleDefinition(0, hum::core::ScaleMode::major);
    const auto chords = hum::core::makeDiatonicTriads(conversion.notes, scale);
    expect(chords.size() == 9, "captured roots expand into three complete chords");
    const std::vector<int> expected_chords{60,64,67,64,67,71,67,71,74};
    for (std::size_t i = 0; i < std::min(chords.size(), expected_chords.size()); ++i) {
        expect(chords[i].pitch == expected_chords[i], "YIN roots map to the expected diatonic chord pitches");
    }
    hum::core::NoteEditor chord_editor;
    expect(chord_editor.addTrack({hum::core::TrackKind::chords, hum::core::InstrumentVoice::grand_piano})
        == hum::core::NoteEditResult::applied, "chord destination is created");
    expect(chord_editor.appendRegion(chords, 0, 1) == hum::core::NoteEditResult::applied,
        "converted chords enter the canonical Playlist region");
    expect(chord_editor.setScale(scale), "chord editor shares the conversion scale");
    chord_editor.setMagnetEnabled(true);
    const auto chord_midi = hum::core::MidiTimeline::fromTimedNotes(chord_editor.projectedNotes(), 96, 480);
    expect(chord_midi && chord_midi->notes.size() == 9, "all chord voices reach shared playback/export data");
    const auto chord_audio = hum::core::SimpleSynthRenderer{}.render(chord_editor.projectedNotes(), {});
    expect(chord_audio && !chord_audio->interleaved_samples.empty(), "captured chord progression renders to local audio");

    hum::core::NoteEditor editor;
    expect(editor.replaceAll(conversion.notes), "conversion loads the canonical editor");
    expect(editor.select(0), "songwriter selects the first note");
    expect(editor.resizeSelected(editor.notes()[0].duration_seconds + 0.05)
            == hum::core::NoteEditResult::applied,
        "songwriter correction updates the canonical phrase");
    expect(editor.setScale(hum::core::makeScaleDefinition(
               0, hum::core::ScaleMode::major)),
        "workflow selects C major");
    editor.setMagnetEnabled(true);
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 60,
        "Magnet audition derives C4 while retaining canonical C#4");

    hum::core::SimpleSynthConfig synth_config;
    synth_config.sample_rate_hz = 48'000;
    synth_config.channel_count = 2;
    const auto render_start = std::chrono::steady_clock::now();
    const auto rendered = hum::core::SimpleSynthRenderer{}.render(
        editor.projectedNotes(), synth_config);
    const auto render_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - render_start).count();
    expect(rendered && !rendered->interleaved_samples.empty(),
        "visible Magnet phrase renders through the built-in instrument");

    hum::core::ProjectDocument project;
    project.tempo_bpm = 96.0;
    project.root_pitch_class = 0;
    project.scale_mode = hum::core::ScaleMode::major;
    project.magnet_enabled = true;
    project.rhythm_snap = {false, 0.125};
    project.canonical_notes = editor.notes();
    const auto project_bytes = hum::core::ProjectFileCodec::encode(project);
    const auto reopened = hum::core::ProjectFileCodec::decode(project_bytes.value_or(""));
    expect(static_cast<bool>(reopened), "saved project reopens through the versioned codec");
    expect(reopened && reopened.project->canonical_notes.size() == editor.notes().size(),
        "reopened project retains the canonical note count");
    expect(reopened && reopened.project->canonical_notes[0].pitch == 61
            && reopened.project->canonical_notes[0].duration_seconds
                == editor.notes()[0].duration_seconds,
        "reopened note data matches the saved correction exactly");

    hum::core::NoteEditor reopened_editor;
    static_cast<void>(reopened_editor.setScale(hum::core::makeScaleDefinition(
        reopened.project->root_pitch_class, reopened.project->scale_mode)));
    reopened_editor.setMagnetEnabled(reopened.project->magnet_enabled);
    expect(reopened_editor.replaceAll(reopened.project->canonical_notes),
        "reopened canonical notes load into a fresh editor");
    expect(reopened_editor.projectedNotes()[0].pitch == 60,
        "reopened audible/export projection matches the saved Magnet state");

    const auto sequence = hum::core::MidiTimeline::fromTimedNotes(
        reopened_editor.projectedNotes(), reopened.project->tempo_bpm,
        reopened.project->ticks_per_quarter);
    const auto midi = sequence
        ? hum::core::StandardMidiFileExporter::serialize(*sequence) : std::nullopt;
    expect(midi.has_value(), "heard Magnet projection exports as valid MIDI bytes");
    if (midi) {
        const auto parsed = independentlyParseMidi(*midi);
        expect(parsed.format == 0 && parsed.tracks == 1 && parsed.division == 480,
            "independent reader validates the format-0 MIDI structure");
        expect(parsed.tempo_microseconds == 625'000,
            "independent reader finds the saved 96 BPM tempo metadata");
        expect(parsed.note_on_pitches.size() == reopened_editor.projectedNotes().size()
                && parsed.note_off_count == reopened_editor.projectedNotes().size(),
            "independent reader finds paired note-on and note-off events");
        expect(!parsed.note_on_pitches.empty() && parsed.note_on_pitches.front() == 60,
            "exported pitch is identical to the audible Magnet projection");
    }

    std::cout << "MVP workflow metric: " << phrase_seconds << " s mono phrase, "
              << fixture.samples.size() * sizeof(float) << " captured bytes, conversion "
              << conversion_ms << " ms, synth render " << render_ms << " ms, rendered "
              << (rendered ? rendered->interleaved_samples.size() * sizeof(float) : 0)
              << " bytes";
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        std::cout << ", process max RSS " << usage.ru_maxrss
#if defined(__APPLE__)
                  << " bytes";
#else
                  << " KiB";
#endif
    }
#endif
    std::cout << '\n';
}

} // namespace

int main() {
    testCompleteFakeHardwareWorkflow();
    if (failures == 0) {
        std::cout << "Complete MVP workflow passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " MVP workflow assertion(s) failed.\n";
    return 1;
}
