#pragma once
#include "hum/core/simple_synth.hpp"
#include "hum/core/voice_clip.hpp"

namespace hum::core {
// An immutable snapshot for one linear take. Canonical project notes are never shifted.
struct OverdubPlan {
    double origin{0}, bpm{120}, duration{30};
    bool recording_click{false};
    std::vector<VoiceClip> voice_clips;
    std::vector<TimedMidiNote> notes;
    std::vector<InstrumentVoice> voices;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] double countInSeconds() const noexcept { return 240.0 / bpm; }
    [[nodiscard]] int countInBeat(double elapsed) const noexcept;
    [[nodiscard]] double projectPosition(double elapsed) const noexcept;
};

[[nodiscard]] std::optional<OverdubPlan> makeOverdubPlan(
    std::span<const TimedMidiNote> notes, std::span<const InstrumentVoice> voices,
    double origin, double bpm, bool recording_click, double duration = 30);
// Adds exactly four clicks and pads the backing to the full capture window.
[[nodiscard]] std::optional<SynthRenderResult> recordingGuide(
    const SynthRenderResult& backing, const OverdubPlan& plan);
}
