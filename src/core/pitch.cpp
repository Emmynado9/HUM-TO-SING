#include "hum/core/pitch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hum::core {

namespace {

[[nodiscard]] std::size_t maximumLag(
    const double sample_rate_hz,
    const double minimum_frequency_hz) noexcept {
    return static_cast<std::size_t>(std::ceil(
        sample_rate_hz / minimum_frequency_hz));
}

[[nodiscard]] std::size_t minimumLag(
    const double sample_rate_hz,
    const double maximum_frequency_hz) noexcept {
    return std::max<std::size_t>(2, static_cast<std::size_t>(std::floor(
        sample_rate_hz / maximum_frequency_hz)));
}

[[nodiscard]] std::size_t decimationFactor(
    const std::uint32_t sample_rate_hz,
    const double maximum_frequency_hz) noexcept {
    const auto target_rate = std::max(12'000.0, maximum_frequency_hz * 6.0);
    return std::max<std::size_t>(
        1, static_cast<std::size_t>(std::floor(sample_rate_hz / target_rate)));
}

} // namespace

bool PitchDetectorConfig::isValid(const std::uint32_t sample_rate_hz) const noexcept {
    if (sample_rate_hz == 0 || frame_size < 32 || hop_size == 0 || hop_size > frame_size
        || !std::isfinite(minimum_frequency_hz) || !std::isfinite(maximum_frequency_hz)
        || minimum_frequency_hz < 20.0 || maximum_frequency_hz <= minimum_frequency_hz
        || maximum_frequency_hz >= static_cast<double>(sample_rate_hz) / 2.0
        || !std::isfinite(input_gate_rms) || input_gate_rms < 0.0F || input_gate_rms > 1.0F
        || !std::isfinite(yin_threshold) || yin_threshold <= 0.0 || yin_threshold >= 1.0
        || !std::isfinite(minimum_voicing_quality) || minimum_voicing_quality < 0.0
        || minimum_voicing_quality > 1.0 || !std::isfinite(reference_a4_hz)
        || reference_a4_hz <= 0.0 || smoothing_window == 0 || smoothing_window > 31) {
        return false;
    }

    const auto decimation = decimationFactor(sample_rate_hz, maximum_frequency_hz);
    const auto analysis_rate = static_cast<double>(sample_rate_hz)
        / static_cast<double>(decimation);
    const auto analysis_frame_size = frame_size / decimation;
    const auto minimum_lag = minimumLag(analysis_rate, maximum_frequency_hz);
    const auto maximum_lag = maximumLag(analysis_rate, minimum_frequency_hz);
    return minimum_lag < maximum_lag && maximum_lag <= analysis_frame_size / 2;
}

RawPitchEstimate YinPitchDetector::estimate(
    const std::span<const float> mono_frame,
    const std::uint32_t sample_rate_hz,
    const PitchDetectorConfig& config,
    const std::uint64_t frame_start_sample) const {
    RawPitchEstimate result;
    result.frame_start_sample = frame_start_sample;
    if (!config.isValid(sample_rate_hz) || mono_frame.size() != config.frame_size) {
        return result;
    }

    long double mean = 0.0L;
    for (const auto sample : mono_frame) {
        if (!std::isfinite(sample)) {
            return result;
        }
        mean += sample;
    }
    mean /= static_cast<long double>(mono_frame.size());

    long double energy = 0.0L;
    for (const auto sample : mono_frame) {
        const auto centered = static_cast<long double>(sample) - mean;
        energy += centered * centered;
    }
    result.rms_level = std::sqrt(static_cast<double>(energy / mono_frame.size()));
    if (result.rms_level < config.input_gate_rms) {
        result.status = PitchEstimateStatus::below_gate;
        return result;
    }

    const auto decimation = decimationFactor(sample_rate_hz, config.maximum_frequency_hz);
    const auto analysis_rate = static_cast<double>(sample_rate_hz)
        / static_cast<double>(decimation);
    const auto analysis_size = mono_frame.size() / decimation;
    std::vector<double> analysis_frame(analysis_size);
    for (std::size_t output_index = 0; output_index < analysis_size; ++output_index) {
        double sum = 0.0;
        const auto input_start = output_index * decimation;
        for (std::size_t index = 0; index < decimation; ++index) {
            sum += mono_frame[input_start + index];
        }
        analysis_frame[output_index] = sum / static_cast<double>(decimation);
    }

    const auto minimum_lag = minimumLag(analysis_rate, config.maximum_frequency_hz);
    const auto maximum_lag = maximumLag(analysis_rate, config.minimum_frequency_hz);
    const auto comparison_count = analysis_frame.size() - maximum_lag;
    std::vector<double> normalized_difference(maximum_lag + 1, 1.0);

    long double cumulative_difference = 0.0L;
    for (std::size_t lag = 1; lag <= maximum_lag; ++lag) {
        long double difference = 0.0L;
        for (std::size_t index = 0; index < comparison_count; ++index) {
            const auto delta = static_cast<long double>(analysis_frame[index])
                - static_cast<long double>(analysis_frame[index + lag]);
            difference += delta * delta;
        }
        cumulative_difference += difference;
        normalized_difference[lag] = cumulative_difference > 0.0L
            ? static_cast<double>(difference * lag / cumulative_difference)
            : 1.0;
    }

    std::size_t candidate = 0;
    for (std::size_t lag = minimum_lag; lag <= maximum_lag; ++lag) {
        if (normalized_difference[lag] < config.yin_threshold) {
            candidate = lag;
            while (candidate < maximum_lag
                && normalized_difference[candidate + 1] < normalized_difference[candidate]) {
                ++candidate;
            }
            break;
        }
    }
    if (candidate == 0) {
        candidate = minimum_lag;
        for (std::size_t lag = minimum_lag + 1; lag <= maximum_lag; ++lag) {
            if (normalized_difference[lag] < normalized_difference[candidate]) {
                candidate = lag;
            }
        }
    }

    result.voicing_quality = std::clamp(1.0 - normalized_difference[candidate], 0.0, 1.0);
    if (result.voicing_quality < config.minimum_voicing_quality) {
        result.status = PitchEstimateStatus::no_periodic_candidate;
        return result;
    }

    double refined_lag = static_cast<double>(candidate);
    if (candidate > 1 && candidate < maximum_lag) {
        const auto left = normalized_difference[candidate - 1];
        const auto middle = normalized_difference[candidate];
        const auto right = normalized_difference[candidate + 1];
        const auto denominator = left - 2.0 * middle + right;
        if (std::abs(denominator) > std::numeric_limits<double>::epsilon()) {
            const auto offset = std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5);
            refined_lag += offset;
        }
    }

    result.frequency_hz = analysis_rate / refined_lag;
    if (!std::isfinite(result.frequency_hz)
        || result.frequency_hz < config.minimum_frequency_hz
        || result.frequency_hz > config.maximum_frequency_hz) {
        result.frequency_hz = 0.0;
        result.status = PitchEstimateStatus::out_of_range;
        return result;
    }
    result.status = PitchEstimateStatus::reliable;
    return result;
}

PitchSmoother::PitchSmoother(const std::size_t window_size)
    : window_size_(std::clamp<std::size_t>(window_size, 1, 31)) {
    midi_history_.reserve(window_size_);
}

DisplayPitchEstimate PitchSmoother::process(
    const RawPitchEstimate& raw,
    const double reference_a4_hz) {
    DisplayPitchEstimate display;
    display.raw = raw;
    const auto raw_midi = frequencyToMidi(raw.frequency_hz, reference_a4_hz);
    if (!raw.isReliable() || !raw_midi) {
        reset();
        return display;
    }

    midi_history_.push_back(*raw_midi);
    if (midi_history_.size() > window_size_) {
        midi_history_.erase(midi_history_.begin());
    }
    auto ordered = midi_history_;
    const auto middle = ordered.begin() + static_cast<std::ptrdiff_t>(ordered.size() / 2);
    std::nth_element(ordered.begin(), middle, ordered.end());
    display.midi_pitch = *middle;
    if (ordered.size() % 2 == 0) {
        display.midi_pitch = (*middle + *std::max_element(ordered.begin(), middle)) / 2.0;
    }

    const auto smoothed_frequency = midiToFrequency(display.midi_pitch, reference_a4_hz);
    if (!smoothed_frequency) {
        reset();
        DisplayPitchEstimate unavailable;
        unavailable.raw = raw;
        return unavailable;
    }
    display.nearest_midi_note = static_cast<int>(std::lround(display.midi_pitch));
    if (display.nearest_midi_note < 0 || display.nearest_midi_note > 127) {
        reset();
        DisplayPitchEstimate unavailable;
        unavailable.raw = raw;
        return unavailable;
    }
    display.reliable = true;
    display.frequency_hz = *smoothed_frequency;
    display.cents_deviation = 100.0
        * (display.midi_pitch - static_cast<double>(display.nearest_midi_note));
    display.note_name = midiNoteName(display.nearest_midi_note);
    return display;
}

void PitchSmoother::reset() noexcept {
    midi_history_.clear();
}

PitchAnalysisRunner::PitchAnalysisRunner(std::shared_ptr<const IPitchDetector> detector)
    : detector_(std::move(detector)) {
    if (!detector_) {
        throw std::invalid_argument("pitch detector is required");
    }
}

std::vector<DisplayPitchEstimate> PitchAnalysisRunner::analyze(
    const AudioRecording& recording,
    const PitchDetectorConfig& config) const {
    std::vector<DisplayPitchEstimate> results;
    const auto raw_results = analyzeRaw(recording, config);
    results.reserve(raw_results.size());
    PitchSmoother smoother(config.smoothing_window);
    for (const auto& raw : raw_results) {
        results.push_back(smoother.process(raw, config.reference_a4_hz));
    }
    return results;
}

std::vector<RawPitchEstimate> PitchAnalysisRunner::analyzeRaw(
    const AudioRecording& recording,
    const PitchDetectorConfig& config,
    ProgressCallback progress,
    CancellationCheck cancelled) const {
    std::vector<RawPitchEstimate> results;
    if (!recording.isValid() || !config.isValid(recording.format().sample_rate_hz)) {
        return results;
    }
    const auto mono = recording.monoSamples();
    if (mono.size() < config.frame_size) {
        return results;
    }

    const auto frame_count = 1 + (mono.size() - config.frame_size) / config.hop_size;
    results.reserve(frame_count);
    if (progress) {
        progress(0, frame_count);
    }
    for (std::size_t start = 0; start + config.frame_size <= mono.size(); start += config.hop_size) {
        if (cancelled && cancelled()) {
            break;
        }
        results.push_back(detector_->estimate(
            std::span<const float>{mono}.subspan(start, config.frame_size),
            recording.format().sample_rate_hz,
            config,
            static_cast<std::uint64_t>(start)));
        if (progress) {
            progress(results.size(), frame_count);
        }
    }
    return results;
}

std::optional<double> frequencyToMidi(
    const double frequency_hz,
    const double reference_a4_hz) noexcept {
    if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0
        || !std::isfinite(reference_a4_hz) || reference_a4_hz <= 0.0) {
        return std::nullopt;
    }
    return 69.0 + 12.0 * std::log2(frequency_hz / reference_a4_hz);
}

std::optional<double> midiToFrequency(
    const double midi_pitch,
    const double reference_a4_hz) noexcept {
    if (!std::isfinite(midi_pitch) || !std::isfinite(reference_a4_hz)
        || reference_a4_hz <= 0.0) {
        return std::nullopt;
    }
    const auto frequency = reference_a4_hz * std::pow(2.0, (midi_pitch - 69.0) / 12.0);
    if (!std::isfinite(frequency) || frequency <= 0.0) {
        return std::nullopt;
    }
    return frequency;
}

std::string midiNoteName(const int midi_note) {
    if (midi_note < 0 || midi_note > 127) {
        return {};
    }
    static constexpr std::array<const char*, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const auto pitch_class = static_cast<std::size_t>(midi_note % 12);
    const auto octave = midi_note / 12 - 1;
    return std::string{names[pitch_class]} + std::to_string(octave);
}

const char* pitchStatusText(const PitchEstimateStatus status) noexcept {
    switch (status) {
    case PitchEstimateStatus::reliable:
        return "Reliable pitch";
    case PitchEstimateStatus::invalid_input:
        return "Invalid analysis input";
    case PitchEstimateStatus::below_gate:
        return "Input is below the gate";
    case PitchEstimateStatus::no_periodic_candidate:
        return "No reliable periodic pitch";
    case PitchEstimateStatus::out_of_range:
        return "Pitch is outside the selected range";
    }
    return "No reliable pitch";
}

} // namespace hum::core
