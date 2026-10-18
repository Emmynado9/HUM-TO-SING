#include "hum/core/simple_synth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
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

void testSampleClockSchedulingAndRelease() {
    hum::core::SimpleSynthConfig config;
    config.sample_rate_hz = 48'000;
    config.channel_count = 2;
    config.attack_seconds = 0.0;
    config.release_seconds = 0.025;
    const std::vector<hum::core::TimedMidiNote> notes{{69, 100, 0.10, 0.20, 0}};
    const auto rendered = hum::core::SimpleSynthRenderer{}.render(notes, config);
    expect(rendered.has_value(), "valid A4 phrase renders");
    if (!rendered) {
        return;
    }
    expect(rendered->format.sample_rate_hz == 48'000
            && rendered->format.channel_count == 2,
        "renderer preserves requested output format");
    expect(rendered->frameCount() == 15'600,
        "render length is phrase end plus a bounded release tail");
    expect(std::abs(rendered->phrase_duration_seconds - 0.30) < 1.0e-12,
        "musical playhead duration excludes release tail");
    expect(std::all_of(rendered->interleaved_samples.cbegin(),
               rendered->interleaved_samples.cbegin() + 4'800 * 2,
               [](const float sample) { return sample == 0.0F; }),
        "samples before the exact onset frame are silent");
    expect(std::any_of(rendered->interleaved_samples.cbegin() + 4'800 * 2,
               rendered->interleaved_samples.cbegin() + 14'400 * 2,
               [](const float sample) { return std::abs(sample) > 0.01F; }),
        "scheduled note produces audible samples");
    expect(std::abs(rendered->interleaved_samples[2'345 * 2]
            - rendered->interleaved_samples[2'345 * 2 + 1]) < 1.0e-7F,
        "procedural mono instrument is copied consistently to stereo");
    expect(std::abs(rendered->interleaved_samples.back()) < 1.0e-7F,
        "release reaches silence so playback cannot leave a sounding voice");
}

void testA4FrequencyAndBounds() {
    expect(std::abs(hum::core::midiPitchFrequency(69) - 440.0) < 1.0e-12,
        "MIDI 69 uses A4 = 440 Hz");
    hum::core::SimpleSynthConfig config;
    config.sample_rate_hz = 48'000;
    config.channel_count = 1;
    config.attack_seconds = 0.0;
    config.release_seconds = 0.0;
    const std::vector<hum::core::TimedMidiNote> notes{{69, 127, 0.0, 1.0, 0}};
    const auto rendered = hum::core::SimpleSynthRenderer{}.render(notes, config);
    expect(rendered.has_value(), "one-second frequency fixture renders");
    std::size_t positive_crossings = 0;
    if (rendered) {
        const auto& samples = rendered->interleaved_samples;
        for (std::size_t index = 1; index < samples.size(); ++index) {
            if (samples[index - 1] <= 0.0F && samples[index] > 0.0F) {
                ++positive_crossings;
            }
        }
    }
    expect(positive_crossings >= 439 && positive_crossings <= 441,
        "procedural instrument oscillates at the scheduled A4 frequency");

    config.maximum_render_seconds = 0.5;
    expect(!hum::core::SimpleSynthRenderer{}.render(notes, config),
        "unsafe render length is rejected before allocation");
    expect(!hum::core::SimpleSynthRenderer{}.render({}, config),
        "empty phrase is rejected with no fabricated audio");

    config.maximum_render_seconds = 1.0;
    const std::vector<hum::core::TimedMidiNote> sub_sample{{69, 96, 0.1, 0.000001, 0}};
    expect(hum::core::SimpleSynthRenderer{}.render(sub_sample, config).has_value(),
        "a valid sub-sample note receives one rendered frame instead of failing");
}

void testOverlapIsBounded() {
    hum::core::SimpleSynthConfig config;
    config.sample_rate_hz = 8'000;
    config.channel_count = 1;
    std::vector<hum::core::TimedMidiNote> notes;
    for (std::uint8_t pitch = 48; pitch < 80; ++pitch) {
        notes.push_back({pitch, 127, 0.0, 0.25, 0});
    }
    const auto rendered = hum::core::SimpleSynthRenderer{}.render(notes, config);
    expect(rendered.has_value(), "overlapping valid notes render independently");
    expect(rendered && std::all_of(rendered->interleaved_samples.cbegin(),
                           rendered->interleaved_samples.cend(),
                           [](const float sample) { return sample >= -1.0F && sample <= 1.0F; }),
        "overlap mix is bounded to the audio sample range");

    config.sample_rate_hz = 384'000;
    config.channel_count = 8;
    const std::vector<hum::core::TimedMidiNote> long_note{{60, 96, 0.0, 10.0, 0}};
    expect(!hum::core::SimpleSynthRenderer{}.render(long_note, config),
        "render rejects an otherwise valid phrase above the PCM allocation bound");
}

void testMuteAndMetronome() {
    using namespace hum::core;
    SimpleSynthConfig config;
    config.sample_rate_hz = 8'000;
    config.channel_count = 1;
    config.release_seconds = 0;
    const std::vector<TimedMidiNote> notes{{69, 100, 0, 1.5, 0, true}};
    const auto muted = SimpleSynthRenderer{}.render(notes, config);
    expect(muted && std::all_of(muted->interleaved_samples.begin(), muted->interleaved_samples.end(),
        [](float sample) { return sample == 0; }), "muted notes render silence without changing duration");
    config.metronome_enabled = true;
    config.tempo_bpm = 120;
    const auto click = SimpleSynthRenderer{}.render({}, config);
    expect(click && click->frameCount() == 16'000 && click->phrase_duration_seconds == 2,
        "empty metronome renders exactly one 4/4 bar for looping");
    if (click) {
        for (const auto start : {0, 4000, 8000, 12000}) {
            expect(std::any_of(click->interleaved_samples.begin() + start,
                click->interleaved_samples.begin() + start + 200,
                [](float sample) { return std::abs(sample) > .001F; }), "each beat contains a click");
            expect(std::all_of(click->interleaved_samples.begin() + start + 200,
                click->interleaved_samples.begin() + start + 4000,
                [](float sample) { return std::abs(sample) < 1e-7F; }), "space between clicks is silent");
        }
    }
    config.tempo_bpm = 60;
    const auto slower = SimpleSynthRenderer{}.render({}, config);
    expect(slower && slower->frameCount() == 32'000, "metronome spacing follows tempo");
    const auto mixed = SimpleSynthRenderer{}.render(notes, config);
    expect(mixed && mixed->phrase_duration_seconds == 1.5,
        "metronome tempo never retimes canonical note boundaries");
    config.tempo_bpm = 0;
    expect(!SimpleSynthRenderer{}.render(notes, config), "invalid metronome tempo is rejected");
}

} // namespace

int main() {
    testMuteAndMetronome();
    testSampleClockSchedulingAndRelease();
    testA4FrequencyAndBounds();
    testOverlapIsBounded();
    if (failures == 0) {
        std::cout << "All simple synth tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " simple synth assertion(s) failed.\n";
    return 1;
}
