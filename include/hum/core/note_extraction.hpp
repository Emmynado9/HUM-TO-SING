#pragma once

#include "hum/core/midi.hpp"
#include "hum/core/pitch.hpp"
#include "hum/core/recording.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace hum::core {

struct NoteExtractionConfig {
    double minimum_note_duration_seconds{0.080};
    double maximum_unreliable_gap_seconds{0.080};
    double transition_hysteresis_cents{75.0};
    std::size_t onset_confirmation_frames{2};
    std::size_t transition_confirmation_frames{2};
    std::uint8_t fixed_velocity{96};
    std::uint8_t midi_channel{0};
    double transition_confirmation_seconds{0.090};
    double octave_confirmation_seconds{0.180};

    [[nodiscard]] bool isValid() const noexcept;
};

class INoteSegmenter {
public:
    virtual ~INoteSegmenter() = default;
    [[nodiscard]] virtual std::vector<TimedMidiNote> segment(
        std::span<const RawPitchEstimate> frames,
        std::uint32_t sample_rate_hz,
        std::size_t frame_size,
        std::size_t hop_size,
        double recording_duration_seconds,
        double reference_a4_hz,
        const NoteExtractionConfig& config) const = 0;
};

class HysteresisNoteSegmenter final : public INoteSegmenter {
public:
    [[nodiscard]] std::vector<TimedMidiNote> segment(
        std::span<const RawPitchEstimate> frames,
        std::uint32_t sample_rate_hz,
        std::size_t frame_size,
        std::size_t hop_size,
        double recording_duration_seconds,
        double reference_a4_hz,
        const NoteExtractionConfig& config) const override;
};

enum class PhraseConversionStatus {
    success,
    empty,
    invalid_input,
    cancelled,
    failed,
};

struct PhraseConversionResult {
    PhraseConversionStatus status{PhraseConversionStatus::invalid_input};
    std::vector<RawPitchEstimate> raw_pitch_frames;
    std::vector<TimedMidiNote> notes;
    MidiSequence midi_sequence;
    std::string message;

    [[nodiscard]] bool hasNotes() const noexcept {
        return status == PhraseConversionStatus::success && !notes.empty();
    }
};

class PhraseConversionPipeline final {
public:
    using ProgressCallback = std::function<void(double fraction)>;
    using CancellationCheck = std::function<bool()>;

    PhraseConversionPipeline(
        std::shared_ptr<const IPitchDetector> detector,
        std::shared_ptr<const INoteSegmenter> segmenter);

    [[nodiscard]] PhraseConversionResult convert(
        const AudioRecording& recording,
        const PitchDetectorConfig& pitch_config,
        const NoteExtractionConfig& extraction_config,
        double tempo_bpm = kDefaultTempoBpm,
        std::uint16_t ticks_per_quarter = kDefaultTicksPerQuarter,
        ProgressCallback progress = {},
        CancellationCheck cancelled = {}) const noexcept;

private:
    std::shared_ptr<const IPitchDetector> detector_;
    std::shared_ptr<const INoteSegmenter> segmenter_;
};

[[nodiscard]] const char* phraseConversionStatusText(PhraseConversionStatus status) noexcept;

} // namespace hum::core
