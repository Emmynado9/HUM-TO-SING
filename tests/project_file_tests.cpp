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
    expect(encoded && encoded->find("audio_storage omitted") != std::string::npos,
        "file explicitly records that original audio is omitted");
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
    encoded.replace(encoded.find("audio_storage omitted"),
        std::string("audio_storage omitted").size(), "audio_storage promised_but_missing");
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

} // namespace

int main() {
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
