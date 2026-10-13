#include "hum/core/audio.hpp"
#include "hum/core/midi.hpp"
#include "hum/core/scale.hpp"
#include "hum/core/services.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;
int checks = 0;

void expect(bool condition, std::string_view message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testMidiValidation() {
    using hum::core::MidiNote;
    using hum::core::TimedMidiNote;

    expect(MidiNote{}.isValid(), "default MIDI note is valid");
    auto invalid_pitch = MidiNote{};
    invalid_pitch.pitch = static_cast<std::uint8_t>(128);
    expect(!invalid_pitch.isValid(), "MIDI pitches are limited to 0..127");
    expect(!MidiNote{60, 0, 0, 480, 0}.isValid(), "velocity zero is reserved for note-off");
    expect(!MidiNote{60, 100, 0, 0, 0}.isValid(), "duration must be positive");
    expect(!MidiNote{60, 100, 0, 480, 16}.isValid(), "MIDI channels are zero-based 0..15");
    expect(TimedMidiNote{69, 96, 0.25, 0.5, 0}.isValid(),
        "seconds-based extracted note accepts MIDI-compatible values");
    expect(!TimedMidiNote{69, 0, 0.25, 0.5, 0}.isValid(),
        "seconds-based note rejects velocity zero");
    expect(!TimedMidiNote{69, 96, -0.1, 0.5, 0}.isValid(),
        "seconds-based note rejects negative start time");
    expect(!TimedMidiNote{69, 96, 0.25, 0.0, 0}.isValid(),
        "seconds-based note requires positive duration");
}

void testTimelineConversions() {
    using hum::core::MidiTimeline;

    const auto half_second = MidiTimeline::ticksToSeconds(480, 120.0, 480);
    expect(half_second.has_value(), "valid tick conversion returns a value");
    expect(half_second && std::abs(*half_second - 0.5) < 1.0e-9,
        "one quarter note at 120 BPM lasts half a second");

    const auto ticks = MidiTimeline::secondsToTicks(0.75, 120.0, 480);
    expect(ticks == 720, "0.75 seconds at 120 BPM converts to 720 ticks");
    expect(!MidiTimeline::secondsToTicks(-0.1, 120.0, 480), "negative time is rejected");
    expect(!MidiTimeline::ticksToSeconds(480, 0.0, 480), "zero tempo is rejected");
    expect(!MidiTimeline::ticksToSeconds(480, 120.0, 0), "zero PPQ is rejected");
}

void testSequenceAndProjectValidation() {
    hum::core::MidiSequence sequence;
    sequence.notes.push_back({64, 96, 240, 480, 0});
    expect(sequence.isValid(), "sequence with one valid note is valid");

    sequence.tempo_bpm = -1.0;
    expect(!sequence.isValid(), "negative sequence tempo is rejected");

    hum::core::Project project;
    expect(project.isValid(), "default project is valid");
    project.schema_version = 0;
    expect(!project.isValid(), "schema version zero is rejected");
}

void testScaleRules() {
    hum::core::ScaleDefinition c_major;
    expect(c_major.isValid(), "C major definition is valid");
    expect(c_major.contains(60), "C major contains middle C");
    expect(!c_major.contains(61), "C major excludes C sharp");
    expect(c_major.nearestPitch(61) == 60, "equal-distance ties resolve downward");
    expect(c_major.nearestPitch(66) == 65, "F sharp quantizes to F in C major");

    hum::core::ScaleDefinition invalid_root{12, {0, 2, 4}};
    hum::core::ScaleDefinition duplicate_interval{0, {0, 2, 2}};
    expect(!invalid_root.isValid(), "scale roots must be pitch classes 0..11");
    expect(!duplicate_interval.isValid(), "scale intervals must be unique");
}

void testAudioBufferValidation() {
    const std::array<float, 4> samples{};
    expect(hum::core::AudioBufferView{samples, 48'000, 1}.isValid(),
        "mono audio with a sample rate is valid");
    expect(!hum::core::AudioBufferView{samples, 48'000, 3}.isValid(),
        "interleaved sample count must divide by the channel count");
}

} // namespace

int main() {
    testMidiValidation();
    testTimelineConversions();
    testSequenceAndProjectValidation();
    testScaleRules();
    testAudioBufferValidation();

    if (failures == 0) {
        std::cout << "All core tests passed (" << checks << " assertions).\n";
        return 0;
    }

    std::cerr << failures << " core test assertion(s) failed.\n";
    return 1;
}
