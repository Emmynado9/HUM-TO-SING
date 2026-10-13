#pragma once

#include "hum/core/recording.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hum::core {

enum class PitchEstimateStatus {
    reliable,
    invalid_input,
    below_gate,
    no_periodic_candidate,
    out_of_range,
};

struct PitchDetectorConfig {
    std::size_t frame_size{4'096};
    std::size_t hop_size{512};
    double minimum_frequency_hz{80.0};
    double maximum_frequency_hz{1'000.0};
    float input_gate_rms{0.01F};
    double yin_threshold{0.15};
    double minimum_voicing_quality{0.80};
    double reference_a4_hz{440.0};
    std::size_t smoothing_window{5};

    [[nodiscard]] bool isValid(std::uint32_t sample_rate_hz) const noexcept;
};

struct RawPitchEstimate {
    PitchEstimateStatus status{PitchEstimateStatus::invalid_input};
    std::uint64_t frame_start_sample{0};
    double frequency_hz{0.0};
    double voicing_quality{0.0};
    double rms_level{0.0};

    [[nodiscard]] bool isReliable() const noexcept {
        return status == PitchEstimateStatus::reliable;
    }
};

struct DisplayPitchEstimate {
    RawPitchEstimate raw;
    bool reliable{false};
    double frequency_hz{0.0};
    double midi_pitch{0.0};
    int nearest_midi_note{0};
    double cents_deviation{0.0};
    std::string note_name;
};

class IPitchDetector {
public:
    virtual ~IPitchDetector() = default;
    [[nodiscard]] virtual RawPitchEstimate estimate(
        std::span<const float> mono_frame,
        std::uint32_t sample_rate_hz,
        const PitchDetectorConfig& config,
        std::uint64_t frame_start_sample = 0) const = 0;
};

class YinPitchDetector final : public IPitchDetector {
public:
    [[nodiscard]] RawPitchEstimate estimate(
        std::span<const float> mono_frame,
        std::uint32_t sample_rate_hz,
        const PitchDetectorConfig& config,
        std::uint64_t frame_start_sample = 0) const override;
};

class PitchSmoother final {
public:
    explicit PitchSmoother(std::size_t window_size = 5);

    [[nodiscard]] DisplayPitchEstimate process(
        const RawPitchEstimate& raw,
        double reference_a4_hz = 440.0);
    void reset() noexcept;

private:
    std::size_t window_size_;
    std::vector<double> midi_history_;
};

class PitchAnalysisRunner final {
public:
    explicit PitchAnalysisRunner(std::shared_ptr<const IPitchDetector> detector);

    using ProgressCallback = std::function<void(std::size_t completed, std::size_t total)>;
    using CancellationCheck = std::function<bool()>;

    [[nodiscard]] std::vector<RawPitchEstimate> analyzeRaw(
        const AudioRecording& recording,
        const PitchDetectorConfig& config,
        ProgressCallback progress = {},
        CancellationCheck cancelled = {}) const;

    [[nodiscard]] std::vector<DisplayPitchEstimate> analyze(
        const AudioRecording& recording,
        const PitchDetectorConfig& config) const;

private:
    std::shared_ptr<const IPitchDetector> detector_;
};

[[nodiscard]] std::optional<double> frequencyToMidi(
    double frequency_hz,
    double reference_a4_hz = 440.0) noexcept;
[[nodiscard]] std::optional<double> midiToFrequency(
    double midi_pitch,
    double reference_a4_hz = 440.0) noexcept;
[[nodiscard]] std::string midiNoteName(int midi_note);
[[nodiscard]] const char* pitchStatusText(PitchEstimateStatus status) noexcept;

} // namespace hum::core
