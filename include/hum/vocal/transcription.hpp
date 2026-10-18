#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace hum::vocal {

struct AudioBuffer {
    std::uint32_t sample_rate{0};
    std::vector<float> samples; // mono, first captured sample is time zero
    [[nodiscard]] double duration() const noexcept {
        return sample_rate ? static_cast<double>(samples.size()) / sample_rate : 0.0;
    }
};

struct PitchFrame {
    double time{0}, midi_pitch{0}, confidence{0}, rms{0};
    bool voiced{false}, onset{false};
};
enum class TokenType { note_start, note_hold, pitch_slide, note_end, rest };
struct MusicalToken {
    TokenType type{TokenType::rest};
    double time{0}, pitch{0}, cents{0}, duration{0};
};
struct NoteEvent {
    int pitch{60};
    double start{0}, duration{0.5};
    int velocity{96};
    bool operator==(const NoteEvent&) const = default;
};
enum class Preset { natural, clean, tight };
struct Config {
    double sensitivity{0.5}, smoothing_ms{35}, gap_ms{40}, minimum_note_ms{60}, hysteresis_ms{45};
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] static Config forPreset(Preset preset) noexcept;
};
struct Correction {
    double bpm{120}, origin{0};
    int quantize_division{0}; // 0, 4, 8, 16; absolute project beat grid
    bool scale_snap{false};
    std::vector<int> scale_pitch_classes{0, 2, 4, 5, 7, 9, 11};
};
enum class Status { success, empty, cancelled, failed };
struct ConversionResult {
    Status status{Status::empty};
    std::vector<PitchFrame> frames;
    std::vector<MusicalToken> tokens;
    std::vector<NoteEvent> notes;
    std::vector<std::string> warnings;
    Config config;
    std::string error;
};
using CancelCheck = std::function<bool()>;
using Progress = std::function<void(double)>;

[[nodiscard]] ConversionResult transcribe(std::span<const PitchFrame> frames,
    double audio_duration, const Config& config, CancelCheck cancelled = {});
[[nodiscard]] std::vector<NoteEvent> reconstruct(std::span<const MusicalToken> tokens);
[[nodiscard]] std::vector<NoteEvent> correct(std::span<const NoteEvent> original,
    const Correction& correction);

} // namespace hum::vocal
