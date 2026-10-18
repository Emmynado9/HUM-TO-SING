#include "hum/core/note_extraction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hum::core {

namespace {

struct PitchObservation {
    int nearest_note{0};
    double midi_pitch{0.0};
    double center_seconds{0.0};
};

struct Candidate {
    int pitch{-1};
    std::size_t count{0};
    double first_center{0.0};
    double last_center{0.0};

    void clear() noexcept {
        pitch = -1;
        count = 0;
        first_center = 0.0;
        last_center = 0.0;
    }

    void observe(const int note, const double center) noexcept {
        if (pitch != note) {
            pitch = note;
            count = 1;
            first_center = center;
        } else {
            ++count;
        }
        last_center = center;
    }
};

struct ActiveNote {
    int pitch{-1};
    double start_seconds{0.0};
    double last_reliable_center{0.0};
};

[[nodiscard]] std::optional<PitchObservation> observationFor(
    const RawPitchEstimate& frame,
    const std::uint32_t sample_rate_hz,
    const std::size_t frame_size,
    const double reference_a4_hz) noexcept {
    if (!frame.isReliable()) {
        return std::nullopt;
    }
    const auto midi = frequencyToMidi(frame.frequency_hz, reference_a4_hz);
    if (!midi || *midi < -0.5 || *midi > 127.5) {
        return std::nullopt;
    }
    const auto nearest = static_cast<int>(std::lround(*midi));
    if (nearest < 0 || nearest > 127) {
        return std::nullopt;
    }
    return PitchObservation{
        nearest,
        *midi,
        (static_cast<double>(frame.frame_start_sample)
            + static_cast<double>(frame_size) / 2.0)
            / static_cast<double>(sample_rate_hz),
    };
}

} // namespace

bool NoteExtractionConfig::isValid() const noexcept {
    return std::isfinite(minimum_note_duration_seconds)
        && minimum_note_duration_seconds > 0.0
        && std::isfinite(maximum_unreliable_gap_seconds)
        && maximum_unreliable_gap_seconds >= 0.0
        && std::isfinite(transition_hysteresis_cents)
        && transition_hysteresis_cents >= 50.0
        && transition_hysteresis_cents <= 200.0
        && onset_confirmation_frames > 0 && onset_confirmation_frames <= 32
        && transition_confirmation_frames > 0 && transition_confirmation_frames <= 32
        && std::isfinite(transition_confirmation_seconds)
        && transition_confirmation_seconds >= 0.0 && transition_confirmation_seconds <= 1.0
        && std::isfinite(octave_confirmation_seconds)
        && octave_confirmation_seconds >= transition_confirmation_seconds && octave_confirmation_seconds <= 1.0
        && fixed_velocity >= 1 && fixed_velocity <= 127
        && midi_channel <= 15;
}

std::vector<TimedMidiNote> HysteresisNoteSegmenter::segment(
    const std::span<const RawPitchEstimate> frames,
    const std::uint32_t sample_rate_hz,
    const std::size_t frame_size,
    const std::size_t hop_size,
    const double recording_duration_seconds,
    const double reference_a4_hz,
    const NoteExtractionConfig& config) const {
    std::vector<TimedMidiNote> notes;
    if (sample_rate_hz == 0 || frame_size == 0 || hop_size == 0
        || !std::isfinite(recording_duration_seconds) || recording_duration_seconds <= 0.0
        || !std::isfinite(reference_a4_hz) || reference_a4_hz <= 0.0
        || !config.isValid()) {
        return notes;
    }

    const auto half_hop = static_cast<double>(hop_size)
        / (2.0 * static_cast<double>(sample_rate_hz));
    const auto hop_seconds = static_cast<double>(hop_size)
        / static_cast<double>(sample_rate_hz);
    Candidate onset;
    Candidate transition;
    std::optional<ActiveNote> active;

    const auto appendActive = [&](const double requested_end) {
        if (!active) {
            return;
        }
        const auto end = std::clamp(requested_end, active->start_seconds, recording_duration_seconds);
        const auto duration = end - active->start_seconds;
        if (duration + 1.0e-12 >= config.minimum_note_duration_seconds) {
            notes.push_back({
                static_cast<std::uint8_t>(active->pitch),
                config.fixed_velocity,
                active->start_seconds,
                duration,
                config.midi_channel,
            });
        }
        active.reset();
    };

    for (const auto& frame : frames) {
        const auto center = (static_cast<double>(frame.frame_start_sample)
            + static_cast<double>(frame_size) / 2.0)
            / static_cast<double>(sample_rate_hz);
        const auto observation = observationFor(
            frame, sample_rate_hz, frame_size, reference_a4_hz);

        if (!observation) {
            onset.clear();
            if (active && center - active->last_reliable_center
                    > config.maximum_unreliable_gap_seconds + hop_seconds + 1e-9) {
                appendActive(active->last_reliable_center + half_hop);
                transition.clear();
            }
            continue;
        }

        if (active && observation->center_seconds - active->last_reliable_center - hop_seconds
                > config.maximum_unreliable_gap_seconds + 1e-9) {
            appendActive(active->last_reliable_center + half_hop);
            transition.clear();
        }

        if (!active) {
            onset.observe(observation->nearest_note, observation->center_seconds);
            if (onset.count >= config.onset_confirmation_frames) {
                active = ActiveNote{
                    onset.pitch,
                    std::clamp(onset.first_center - half_hop, 0.0, recording_duration_seconds),
                    onset.last_center,
                };
                onset.clear();
            }
            continue;
        }

        const auto distance_cents = 100.0
            * std::abs(observation->midi_pitch - static_cast<double>(active->pitch));
        // Any voiced observation sustains the current note while a change is being verified.
        // Otherwise an unconfirmed wobble is incorrectly treated as missing audio.
        active->last_reliable_center = observation->center_seconds;
        if (distance_cents < config.transition_hysteresis_cents
            || observation->nearest_note == active->pitch) {
            active->last_reliable_center = observation->center_seconds;
            transition.clear();
            continue;
        }

        transition.observe(observation->nearest_note, observation->center_seconds);
        const auto confirmation = std::abs(transition.pitch - active->pitch) >= 12
            ? config.octave_confirmation_seconds : config.transition_confirmation_seconds;
        const auto evidence_seconds = transition.count * hop_seconds;
        if (transition.count < config.transition_confirmation_frames
            || evidence_seconds + 1e-9 < confirmation) {
            continue;
        }

        const auto boundary = std::clamp(
            transition.first_center - half_hop, 0.0, recording_duration_seconds);
        appendActive(boundary);
        active = ActiveNote{transition.pitch, boundary, transition.last_center};
        transition.clear();
    }

    if (active) {
        appendActive(active->last_reliable_center + half_hop);
    }

    std::stable_sort(notes.begin(), notes.end(), [](const TimedMidiNote& left, const TimedMidiNote& right) {
        if (left.start_seconds != right.start_seconds) {
            return left.start_seconds < right.start_seconds;
        }
        return left.pitch < right.pitch;
    });
    return notes;
}

PhraseConversionPipeline::PhraseConversionPipeline(
    std::shared_ptr<const IPitchDetector> detector,
    std::shared_ptr<const INoteSegmenter> segmenter)
    : detector_(std::move(detector)),
      segmenter_(std::move(segmenter)) {
    if (!detector_ || !segmenter_) {
        throw std::invalid_argument("pitch detector and note segmenter are required");
    }
}

PhraseConversionResult PhraseConversionPipeline::convert(
    const AudioRecording& recording,
    const PitchDetectorConfig& pitch_config,
    const NoteExtractionConfig& extraction_config,
    const double tempo_bpm,
    const std::uint16_t ticks_per_quarter,
    ProgressCallback progress,
    CancellationCheck cancelled) const noexcept {
    PhraseConversionResult result;
    const auto report = [&progress](const double fraction) {
        if (progress) {
            progress(std::clamp(fraction, 0.0, 1.0));
        }
    };
    const auto isCancelled = [&cancelled] {
        return cancelled && cancelled();
    };

    try {
        report(0.0);
        if (!recording.isValid()
            || !pitch_config.isValid(recording.format().sample_rate_hz)
            || !extraction_config.isValid()
            || !MidiSequence{ticks_per_quarter, tempo_bpm, {}}.isValid()) {
            result.status = PhraseConversionStatus::invalid_input;
            result.message = "The recording or conversion settings are invalid.";
            return result;
        }
        if (isCancelled()) {
            result.status = PhraseConversionStatus::cancelled;
            result.message = "Conversion was cancelled.";
            return result;
        }
        if (recording.frameCount() < pitch_config.frame_size) {
            result.status = PhraseConversionStatus::empty;
            result.message = "The recording is shorter than one analysis window.";
            report(1.0);
            return result;
        }

        const PitchAnalysisRunner runner(detector_);
        result.raw_pitch_frames = runner.analyzeRaw(
            recording,
            pitch_config,
            [&report](const std::size_t completed, const std::size_t total) {
                const auto fraction = total == 0 ? 1.0
                    : static_cast<double>(completed) / static_cast<double>(total);
                report(0.82 * fraction);
            },
            isCancelled);
        if (isCancelled()) {
            result.status = PhraseConversionStatus::cancelled;
            result.message = "Conversion was cancelled; the recording remains available.";
            return result;
        }

        result.notes = segmenter_->segment(
            result.raw_pitch_frames,
            recording.format().sample_rate_hz,
            pitch_config.frame_size,
            pitch_config.hop_size,
            recording.durationSeconds(),
            pitch_config.reference_a4_hz,
            extraction_config);
        report(0.92);
        const auto sequence = MidiTimeline::fromTimedNotes(
            result.notes, tempo_bpm, ticks_per_quarter);
        if (!sequence) {
            result.status = PhraseConversionStatus::failed;
            result.message = "The extracted notes could not be represented safely as MIDI ticks.";
            return result;
        }
        result.midi_sequence = *sequence;
        report(1.0);
        if (result.notes.empty()) {
            result.status = PhraseConversionStatus::empty;
            result.message = "No stable voiced notes met the extraction thresholds.";
            return result;
        }
        result.status = PhraseConversionStatus::success;
        result.message = "Phrase conversion completed.";
        return result;
    } catch (...) {
        result.status = PhraseConversionStatus::failed;
        result.message = "Phrase conversion failed unexpectedly.";
        return result;
    }
}

const char* phraseConversionStatusText(const PhraseConversionStatus status) noexcept {
    switch (status) {
    case PhraseConversionStatus::success:
        return "Completed";
    case PhraseConversionStatus::empty:
        return "No notes found";
    case PhraseConversionStatus::invalid_input:
        return "Invalid input";
    case PhraseConversionStatus::cancelled:
        return "Cancelled";
    case PhraseConversionStatus::failed:
        return "Failed";
    }
    return "Failed";
}

} // namespace hum::core
