#include "hum/core/pitch.hpp"
#include "hum/core/wave_fixture.hpp"

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
#include <random>
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

[[nodiscard]] hum::core::PitchDetectorConfig testConfig() {
    hum::core::PitchDetectorConfig config;
    config.frame_size = 4'096;
    config.hop_size = 512;
    config.minimum_frequency_hz = 80.0;
    config.maximum_frequency_hz = 1'000.0;
    config.input_gate_rms = 0.01F;
    config.smoothing_window = 5;
    return config;
}

[[nodiscard]] std::vector<float> sineTone(
    const std::uint32_t sample_rate,
    const double frequency_hz,
    const double seconds,
    const float amplitude,
    const double vibrato_cents = 0.0,
    const double vibrato_rate_hz = 5.0) {
    const auto sample_count = static_cast<std::size_t>(std::llround(seconds * sample_rate));
    std::vector<float> samples(sample_count);
    double phase = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto time = static_cast<double>(index) / sample_rate;
        const auto cents = vibrato_cents * std::sin(2.0 * std::numbers::pi * vibrato_rate_hz * time);
        const auto instantaneous_frequency = frequency_hz * std::pow(2.0, cents / 1'200.0);
        phase += 2.0 * std::numbers::pi * instantaneous_frequency / sample_rate;
        samples[index] = amplitude * static_cast<float>(std::sin(phase));
    }
    return samples;
}

[[nodiscard]] std::vector<hum::core::DisplayPitchEstimate> analyze(
    std::vector<float> samples,
    const hum::core::PitchDetectorConfig& config,
    const std::uint32_t sample_rate = 48'000) {
    const hum::core::AudioRecording recording({sample_rate, 1}, std::move(samples));
    const hum::core::PitchAnalysisRunner runner(
        std::make_shared<hum::core::YinPitchDetector>());
    return runner.analyze(recording, config);
}

[[nodiscard]] double centsError(const double actual_hz, const double expected_hz) {
    return 1'200.0 * std::log2(actual_hz / expected_hz);
}

[[nodiscard]] double medianAbsoluteError(
    const std::vector<hum::core::DisplayPitchEstimate>& results,
    const double expected_hz,
    const std::uint32_t sample_rate,
    const std::size_t frame_size,
    const double warmup_seconds = 0.15) {
    std::vector<double> errors;
    for (const auto& result : results) {
        const auto center_seconds = (static_cast<double>(result.raw.frame_start_sample)
            + static_cast<double>(frame_size) / 2.0) / sample_rate;
        if (result.reliable && center_seconds >= warmup_seconds) {
            errors.push_back(std::abs(centsError(result.frequency_hz, expected_hz)));
        }
    }
    if (errors.empty()) {
        return 1'000'000.0;
    }
    const auto middle = errors.begin() + static_cast<std::ptrdiff_t>(errors.size() / 2);
    std::nth_element(errors.begin(), middle, errors.end());
    return *middle;
}

[[nodiscard]] double medianSignedError(
    const std::vector<hum::core::DisplayPitchEstimate>& results,
    const double expected_hz,
    const std::uint32_t sample_rate,
    const std::size_t frame_size,
    const double warmup_seconds) {
    std::vector<double> errors;
    for (const auto& result : results) {
        const auto center_seconds = (static_cast<double>(result.raw.frame_start_sample)
            + static_cast<double>(frame_size) / 2.0) / sample_rate;
        if (result.reliable && center_seconds >= warmup_seconds) {
            errors.push_back(centsError(result.frequency_hz, expected_hz));
        }
    }
    if (errors.empty()) {
        return 1'000'000.0;
    }
    const auto middle = errors.begin() + static_cast<std::ptrdiff_t>(errors.size() / 2);
    std::nth_element(errors.begin(), middle, errors.end());
    return *middle;
}

void writeU16(std::ofstream& output, const std::uint16_t value) {
    const char bytes[2]{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
    };
    output.write(bytes, 2);
}

void writeU32(std::ofstream& output, const std::uint32_t value) {
    const char bytes[4]{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU),
    };
    output.write(bytes, 4);
}

void writePcm16Wave(
    const std::filesystem::path& path,
    const std::vector<float>& samples,
    const std::uint32_t sample_rate) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    output.write("RIFF", 4);
    writeU32(output, 36U + data_bytes);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16);
    writeU16(output, 1);
    writeU16(output, 1);
    writeU32(output, sample_rate);
    writeU32(output, sample_rate * 2U);
    writeU16(output, 2);
    writeU16(output, 16);
    output.write("data", 4);
    writeU32(output, data_bytes);
    for (const auto sample : samples) {
        const auto value = static_cast<std::int16_t>(std::lround(
            std::clamp(sample, -1.0F, 1.0F) * 32'767.0F));
        writeU16(output, static_cast<std::uint16_t>(value));
    }
}

void testPitchMath() {
    const auto a4 = hum::core::frequencyToMidi(440.0);
    expect(a4.has_value(), "A4 frequency converts to MIDI");
    expectNear(a4.value_or(0.0), 69.0, 1.0e-12, "A4 is MIDI note 69");
    expectNear(hum::core::midiToFrequency(69.0).value_or(0.0), 440.0, 1.0e-12,
        "MIDI note 69 converts to A4 at 440 Hz");
    expect(hum::core::midiNoteName(69) == "A4", "MIDI note 69 is named A4");
    expect(!hum::core::frequencyToMidi(0.0), "zero frequency is rejected");
}

void testSteadyTones() {
    constexpr std::uint32_t sample_rate = 48'000;
    const auto config = testConfig();
    for (const auto frequency : {
             82.406889, 110.0, 220.0, 261.625565, 440.0, 659.255114, 880.0, 987.766603}) {
        const auto results = analyze(sineTone(sample_rate, frequency, 0.45, 0.30F), config);
        const auto error = medianAbsoluteError(
            results, frequency, sample_rate, config.frame_size);
        std::cout << "steady " << frequency << " Hz median error: " << error << " cents\n";
        expect(error < 20.0, "steady-tone median error is below 20 cents after warm-up");
    }

    const auto a4_results = analyze(sineTone(sample_rate, 440.0, 0.45, 0.25F), config);
    const auto reliable = std::find_if(a4_results.crbegin(), a4_results.crend(),
        [](const auto& result) { return result.reliable; });
    expect(reliable != a4_results.crend(), "A4 produces a reliable display estimate");
    expect(reliable != a4_results.crend() && reliable->nearest_midi_note == 69,
        "A4 selects MIDI note 69");
    expect(reliable != a4_results.crend() && reliable->note_name == "A4",
        "A4 displays the expected note name");
    expect(reliable != a4_results.crend() && std::abs(reliable->cents_deviation) < 20.0,
        "A4 cents are measured relative to its nearest note");
}

void testSilenceNoiseAndGate() {
    constexpr std::uint32_t sample_rate = 48'000;
    const auto config = testConfig();
    const auto silence = analyze(std::vector<float>(sample_rate / 2U, 0.0F), config);
    expect(std::none_of(silence.cbegin(), silence.cend(),
        [](const auto& result) { return result.reliable; }),
        "silence produces no reliable note");
    expect(std::all_of(silence.cbegin(), silence.cend(), [](const auto& result) {
        return result.raw.status == hum::core::PitchEstimateStatus::below_gate;
    }), "silence is explicitly classified below the gate");

    std::mt19937 generator(0x5eedU);
    std::uniform_real_distribution<float> distribution(-0.20F, 0.20F);
    std::vector<float> noise(sample_rate);
    std::generate(noise.begin(), noise.end(), [&] { return distribution(generator); });
    const auto noise_results = analyze(std::move(noise), config);
    const auto false_detections = static_cast<std::size_t>(std::count_if(
        noise_results.cbegin(), noise_results.cend(),
        [](const auto& result) { return result.reliable; }));
    const auto false_rate = noise_results.empty() ? 1.0
        : static_cast<double>(false_detections) / static_cast<double>(noise_results.size());
    std::cout << "seeded-noise reliable frames: " << false_detections << '/'
              << noise_results.size() << " (" << false_rate * 100.0 << "%)\n";
    expect(false_rate <= 0.05, "seeded-noise false detection rate is at most 5 percent");

    const auto quiet_samples = sineTone(sample_rate, 440.0, 0.45, 0.002F);
    const auto gated = analyze(quiet_samples, config);
    expect(std::none_of(gated.cbegin(), gated.cend(),
        [](const auto& result) { return result.reliable; }),
        "a tone below the configured gate produces no reliable note");

    auto lower_gate = config;
    lower_gate.input_gate_rms = 0.001F;
    const auto audible = analyze(quiet_samples, lower_gate);
    expect(medianAbsoluteError(audible, 440.0, sample_rate, lower_gate.frame_size) < 20.0,
        "lowering the gate intentionally admits the low-level tone");
}

void testAmplitudeChangesAndVibrato() {
    constexpr std::uint32_t sample_rate = 48'000;
    const auto config = testConfig();
    auto changing = sineTone(sample_rate, 220.0, 0.80, 0.05F);
    for (std::size_t index = changing.size() / 2; index < changing.size(); ++index) {
        changing[index] *= 8.0F;
    }
    const auto changing_results = analyze(std::move(changing), config);
    expect(medianAbsoluteError(changing_results, 220.0, sample_rate, config.frame_size) < 20.0,
        "amplitude changes preserve median pitch accuracy");

    const auto vibrato = analyze(
        sineTone(sample_rate, 220.0, 1.0, 0.25F, 30.0, 5.0), config);
    const auto center_bias = medianSignedError(
        vibrato, 220.0, sample_rate, config.frame_size, 0.15);
    const auto reliable_count = std::count_if(vibrato.cbegin(), vibrato.cend(),
        [](const auto& result) { return result.reliable; });
    const auto reliable_rate = vibrato.empty() ? 0.0
        : static_cast<double>(reliable_count) / static_cast<double>(vibrato.size());
    std::cout << "vibrato median center bias: " << center_bias
              << " cents; reliable frames: " << reliable_rate * 100.0 << "%\n";
    expect(std::abs(center_bias) < 10.0,
        "vibrato has less than 10 cents median center bias after warm-up");
    expect(reliable_rate >= 0.80, "vibrato remains reliably voiced in at least 80 percent of frames");
}

void testConfigurationAndSmoothing() {
    auto config = testConfig();
    expect(config.isValid(48'000), "default test frame and vocal range are valid at 48 kHz");
    auto alternate = config;
    alternate.frame_size = 2'048;
    alternate.hop_size = 256;
    alternate.minimum_frequency_hz = 100.0;
    const auto alternate_samples = sineTone(48'000, 440.0, 0.25, 0.20F);
    const auto alternate_results = analyze(alternate_samples, alternate);
    const auto expected_frames = 1U
        + (alternate_samples.size() - alternate.frame_size) / alternate.hop_size;
    expect(alternate_results.size() == expected_frames,
        "configured frame and hop sizes determine stored-fixture analysis cadence");
    config.frame_size = 1'024;
    expect(!config.isValid(48'000), "a frame too short for the low-frequency bound is rejected");

    hum::core::PitchSmoother smoother(3);
    hum::core::RawPitchEstimate raw;
    raw.status = hum::core::PitchEstimateStatus::reliable;
    raw.frequency_hz = 440.0;
    raw.voicing_quality = 0.99;
    const auto first = smoother.process(raw);
    raw.frequency_hz = hum::core::midiToFrequency(70.0).value();
    static_cast<void>(smoother.process(raw));
    raw.frequency_hz = 440.0;
    const auto third = smoother.process(raw);
    expect(first.raw.frequency_hz == 440.0 && third.raw.frequency_hz == 440.0,
        "raw detector estimates remain present beside display smoothing");
    expect(third.nearest_midi_note == 69,
        "median display smoothing resists a one-frame semitone excursion");
    raw.status = hum::core::PitchEstimateStatus::below_gate;
    raw.frequency_hz = 0.0;
    expect(!smoother.process(raw).reliable,
        "an unreliable raw estimate immediately displays No reliable pitch");
}

void testStoredWaveFixturePath() {
    constexpr std::uint32_t sample_rate = 48'000;
    const auto fixture = std::filesystem::temp_directory_path()
        / "hum-to-sing-a4-pitch-fixture.wav";
    writePcm16Wave(fixture, sineTone(sample_rate, 440.0, 0.45, 0.25F), sample_rate);
    const auto loaded = hum::core::loadWaveFixture(fixture);
    expect(static_cast<bool>(loaded), "stored PCM16 WAV fixture loads");
    expect(loaded && loaded.recording->format().sample_rate_hz == sample_rate,
        "stored fixture preserves its sample rate");
    const hum::core::PitchAnalysisRunner runner(
        std::make_shared<hum::core::YinPitchDetector>());
    const auto results = loaded ? runner.analyze(*loaded.recording, testConfig())
                                : std::vector<hum::core::DisplayPitchEstimate>{};
    expect(medianAbsoluteError(results, 440.0, sample_rate, testConfig().frame_size) < 20.0,
        "stored fixture runs through the same detector and smoothing path");
    std::error_code cleanup_error;
    std::filesystem::remove(fixture, cleanup_error);
    expect(!cleanup_error, "generated stored-audio fixture is cleaned up");
}

void measureFixtureRuntime() {
    constexpr std::uint32_t sample_rate = 48'000;
    const auto config = testConfig();
    const auto fixture = sineTone(sample_rate, 220.0, 2.0, 0.25F);
    const auto started = std::chrono::steady_clock::now();
    const auto results = analyze(fixture, config);
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "runtime: 2.0 seconds of 48 kHz mono in " << elapsed
              << " seconds (" << results.size() << " frames); window "
              << 1'000.0 * config.frame_size / sample_rate << " ms; hop "
              << 1'000.0 * config.hop_size / sample_rate << " ms\n";
    expect(!results.empty(), "runtime fixture produces analysis frames");
}

} // namespace

int main() {
    testPitchMath();
    testSteadyTones();
    testSilenceNoiseAndGate();
    testAmplitudeChangesAndVibrato();
    testConfigurationAndSmoothing();
    testStoredWaveFixturePath();
    measureFixtureRuntime();

    if (failures == 0) {
        std::cout << "All pitch tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " pitch assertion(s) failed.\n";
    return 1;
}
