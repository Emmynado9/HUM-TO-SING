#include "hum/core/project_file.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

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

[[nodiscard]] hum::core::ProjectDocument fixture() {
    hum::core::ProjectDocument project;
    project.tempo_bpm = 93.75;
    project.ticks_per_quarter = 960;
    project.root_pitch_class = 10;
    project.scale_mode = hum::core::ScaleMode::natural_minor;
    project.magnet_enabled = true;
    project.metronome_enabled = true;
    project.grid_division = 32;
    project.rhythm_snap = {true, 0.25};
    project.canonical_notes = {
        {61, 84, 0.123456789, 0.456789123, 0},
        {72, 111, 1.25, 0.75, 0, true},
    };
    return project;
}

void testRoundTripPreservesCanonicalDocument() {
    const auto source = fixture();
    const auto encoded = hum::core::ProjectFileCodec::encode(source);
    expect(encoded.has_value(), "valid project encodes");
    expect(encoded && encoded->find("audio_storage embedded_voice") != std::string::npos,
        "file explicitly records embedded Playlist voice storage");
    const auto decoded = hum::core::ProjectFileCodec::decode(encoded.value_or(""));
    expect(static_cast<bool>(decoded), "encoded project decodes");
    if (!decoded) {
        return;
    }
    const auto& loaded = *decoded.project;
    expect(loaded.schema_version == hum::core::kCurrentProjectSchemaVersion,
        "schema version round-trips");
    expect(std::abs(loaded.tempo_bpm - source.tempo_bpm) < 1.0e-12,
        "tempo round-trips");
    expect(loaded.ticks_per_quarter == source.ticks_per_quarter,
        "tick resolution round-trips");
    expect(loaded.root_pitch_class == source.root_pitch_class
            && loaded.scale_mode == source.scale_mode,
        "key and scale round-trip");
    expect(loaded.metronome_enabled && loaded.grid_division == 32,
        "metronome and musical grid settings round-trip");
    expect(loaded.magnet_enabled == source.magnet_enabled,
        "Magnet state round-trips");
    expect(loaded.rhythm_snap.enabled == source.rhythm_snap.enabled
            && loaded.rhythm_snap.interval_seconds == source.rhythm_snap.interval_seconds,
        "rhythm editing setting round-trips");
    expect(loaded.canonical_notes.size() == source.canonical_notes.size(),
        "canonical note count round-trips");
    for (std::size_t index = 0; index < source.canonical_notes.size(); ++index) {
        const auto& expected = source.canonical_notes[index];
        const auto& actual = loaded.canonical_notes[index];
        expect(actual.pitch == expected.pitch && actual.velocity == expected.velocity
                && actual.channel == expected.channel
                && actual.start_seconds == expected.start_seconds
                && actual.duration_seconds == expected.duration_seconds && actual.muted == expected.muted,
            "canonical note fields round-trip exactly");
    }
}

void testCorruptionAndVersionsFailClearly() {
    const auto unsupported = hum::core::ProjectFileCodec::decode(
        "HUM_TO_SING_PROJECT 99\n");
    expect(unsupported.status == hum::core::ProjectDecodeStatus::unsupported_version,
        "future schema version is distinguished from corruption");
    expect(!unsupported.message.empty(), "unsupported version has an actionable message");

    const auto not_project = hum::core::ProjectFileCodec::decode("hello\n");
    expect(not_project.status == hum::core::ProjectDecodeStatus::corrupt,
        "unrecognized file is rejected as corrupt");

    auto encoded = *hum::core::ProjectFileCodec::encode(fixture());
    encoded.replace(encoded.find("audio_storage embedded_voice"),
        std::string("audio_storage embedded_voice").size(), "audio_storage promised_but_missing");
    expect(!hum::core::ProjectFileCodec::decode(encoded),
        "unsupported audio-storage promise is not silently accepted");

    auto trailing = *hum::core::ProjectFileCodec::encode(fixture());
    trailing += "unexpected\n";
    expect(!hum::core::ProjectFileCodec::decode(trailing),
        "trailing unknown content is rejected instead of ignored");
}

void testInvalidValuesDoNotEncodeOrLoad() {
    auto invalid = fixture();
    invalid.tempo_bpm = 0.0;
    expect(!hum::core::ProjectFileCodec::encode(invalid), "invalid tempo does not encode");
    invalid = fixture();
    invalid.grid_division = 3;
    expect(!hum::core::ProjectFileCodec::encode(invalid), "unsupported grid division is rejected");
    invalid = fixture();
    invalid.canonical_notes[0].duration_seconds = 0.0;
    expect(!hum::core::ProjectFileCodec::encode(invalid), "invalid canonical note does not encode");

    auto encoded = *hum::core::ProjectFileCodec::encode(fixture());
    const auto pitch_position = encoded.find("note 61");
    encoded.replace(pitch_position, std::string("note 61").size(), "note 255");
    expect(!hum::core::ProjectFileCodec::decode(encoded),
        "out-of-range MIDI pitch in a file is rejected");
}

void testSongwritingData() {
    using namespace hum::core;
    auto project = fixture();
    project.lyrics = "[Verse]\nCafé — my \"song\"\n\\ repeat\n\n[Chorus]\n你好";
    VoiceClip clip; clip.start_seconds = 2.125; clip.sample_rate = 8000; clip.muted = true;
    clip.samples = {-32768, -1, 0, 1, 32767}; project.voice_clips = {clip};
    const auto encoded = *ProjectFileCodec::encode(project);
    const auto loaded = ProjectFileCodec::decode(encoded);
    expect(loaded && loaded.project->lyrics == project.lyrics, "Unicode multiline lyrics, quotes and slashes round-trip");
    expect(loaded && loaded.project->voice_clips[0].samples == clip.samples
        && loaded.project->voice_clips[0].muted && loaded.project->voice_clips[0].start_seconds == 2.125,
        "signed PCM16 samples, voice mute and timing round-trip exactly");
    auto bad = encoded; bad.erase(bad.find("8000ffff000000017fff"), 1);
    expect(!ProjectFileCodec::decode(bad), "truncated voice PCM is rejected");
    bad = encoded; bad.replace(bad.find("voice_count 1"), 13, "voice_count 999");
    expect(!ProjectFileCodec::decode(bad), "unbounded voice count is rejected");
    auto legacy = *ProjectFileCodec::encode(fixture());
    legacy.replace(legacy.find("PROJECT 6"), 9, "PROJECT 5");
    legacy.replace(legacy.find("audio_storage embedded_voice"), std::string("audio_storage embedded_voice").size(), "audio_storage omitted");
    legacy.erase(legacy.find("lyrics "), legacy.find("end\n") - legacy.find("lyrics "));
    const auto old = ProjectFileCodec::decode(legacy);
    expect(old && old.project->lyrics.empty() && old.project->voice_clips.empty(), "version 5 opens with blank lyrics and no voice audio");
    project.lyrics.assign(1024U * 1024U + 1, 'x');
    expect(!ProjectFileCodec::encode(project), "oversized lyrics cannot silently create an unreadable project");
    clip.muted = false; clip.samples.assign(8000, 16384);
    SynthRenderResult mix{{16000, 2}, {}, 0};
    expect(mixVoiceClips(mix, {clip}, 2.625, 1), "voice mixes into an overdub window with resampling");
    expect(mix.frameCount() == 8000 && std::abs(mix.interleaved_samples[100] - .5F) < .001,
        "seek into sustained voice trims the beginning and preserves amplitude and duration");
    clip.muted = true; SynthRenderResult silent{{16000, 2}, {}, 0};
    expect(mixVoiceClips(silent, {clip}) && silent.interleaved_samples.empty(), "muted audio is excluded from playback and backing");
    clip.muted = false; SynthRenderResult late{{192000, 2}, {}, 0}; clip.start_seconds = 599;
    expect(!mixVoiceClips(late, {clip}), "oversized render fails safely before allocation");
}
} // namespace

int main() {
    testSongwritingData();
    testRoundTripPreservesCanonicalDocument();
    testCorruptionAndVersionsFailClearly();
    testInvalidValuesDoNotEncodeOrLoad();
    if (failures == 0) {
        std::cout << "All project file tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " project file assertion(s) failed.\n";
    return 1;
}
