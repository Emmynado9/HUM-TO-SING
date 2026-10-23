#pragma once
#include "hum/core/recording.hpp"
#include "hum/core/simple_synth.hpp"
#include <string>

namespace hum::core {
// Canonical local voice audio: mono PCM16, independent of MIDI and UI state.
struct VoiceClip {
    std::string name{"Voice"};
    double start_seconds{0};
    std::uint32_t sample_rate{48000};
    bool muted{false};
    std::vector<std::int16_t> samples;
    [[nodiscard]] double duration() const noexcept { return sample_rate ? double(samples.size()) / sample_rate : 0; }
    [[nodiscard]] bool valid() const noexcept;
};
inline constexpr std::size_t kMaximumVoiceSamples = 8U * 1024U * 1024U;
[[nodiscard]] bool validVoiceClips(const std::vector<VoiceClip>& clips) noexcept;
[[nodiscard]] std::optional<VoiceClip> makeVoiceClip(const AudioRecording& audio, double origin);
// Mix a project window into an existing render; used by playback and overdub alike.
[[nodiscard]] bool mixVoiceClips(SynthRenderResult& render, const std::vector<VoiceClip>& clips,
    double origin = 0, double duration = 600);
}
