#pragma once

#include "hum/core/audio.hpp"
#include "hum/core/midi.hpp"
#include "hum/core/note_editor.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <cstddef>
#include <vector>

namespace hum::core {

struct SimpleSynthConfig {
    std::uint32_t sample_rate_hz{48'000};
    std::uint16_t channel_count{2};
    float master_gain{0.20F};
    double attack_seconds{0.006};
    double release_seconds{0.030};
    double maximum_render_seconds{600.0};
    std::size_t maximum_interleaved_samples{16U * 1024U * 1024U};
    bool metronome_enabled{false};
    double tempo_bpm{kDefaultTempoBpm};
    std::vector<InstrumentVoice> note_voices;

    [[nodiscard]] bool isValid() const noexcept;
};

struct SynthRenderResult {
    AudioFormat format;
    std::vector<float> interleaved_samples;
    double phrase_duration_seconds{0.0};

    [[nodiscard]] std::uint64_t frameCount() const noexcept;
    [[nodiscard]] double renderedDurationSeconds() const noexcept;
};

class SimpleSynthRenderer final {
public:
    [[nodiscard]] std::optional<SynthRenderResult> render(
        std::span<const TimedMidiNote> notes,
        const SimpleSynthConfig& config = {}) const;
};

[[nodiscard]] double midiPitchFrequency(std::uint8_t pitch, double a4_hz = 440.0) noexcept;
[[nodiscard]] float metronomeSample(double seconds, double tempo_bpm) noexcept;

} // namespace hum::core
