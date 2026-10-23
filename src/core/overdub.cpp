#include "hum/core/overdub.hpp"
#include <algorithm>
#include <cmath>

namespace hum::core {
bool OverdubPlan::valid() const noexcept {
    return std::isfinite(origin) && origin >= 0 && std::isfinite(duration) && duration > 0
        && duration <= 30 && origin + duration <= 600 && std::isfinite(bpm) && bpm >= 20 && bpm <= 400
        && validVoiceClips(voice_clips) && notes.size() == voices.size()
        && std::all_of(notes.begin(), notes.end(), [this](const auto& note) {
            return note.isValid() && note.start_seconds + note.duration_seconds <= duration + 1e-8;
        }) && std::all_of(voices.begin(), voices.end(), [](auto voice) { return voice <= InstrumentVoice::strings; });
}
int OverdubPlan::countInBeat(double elapsed) const noexcept {
    if (!valid() || !std::isfinite(elapsed) || elapsed < 0 || elapsed >= countInSeconds()) { return 0; }
    return std::min(4, 1 + static_cast<int>(std::floor(elapsed * bpm / 60)));
}
double OverdubPlan::projectPosition(double elapsed) const noexcept {
    return origin + (std::isfinite(elapsed) ? std::clamp(elapsed - countInSeconds(), 0.0, duration) : 0.0);
}
std::optional<OverdubPlan> makeOverdubPlan(std::span<const TimedMidiNote> notes,
    std::span<const InstrumentVoice> voices, double origin, double bpm, bool click, double duration) {
    OverdubPlan plan; plan.origin = origin; plan.bpm = bpm; plan.duration = duration; plan.recording_click = click;
    if (!plan.valid() || (!voices.empty() && voices.size() != notes.size())) { return std::nullopt; }
    for (std::size_t i = 0; i < notes.size(); ++i) {
        auto note = notes[i];
        if (!note.isValid()) { return std::nullopt; }
        const auto first = std::max(origin, note.start_seconds);
        const auto last = std::min(origin + duration, note.start_seconds + note.duration_seconds);
        if (last <= first || note.muted) { continue; }
        note.start_seconds = first - origin; note.duration_seconds = last - first;
        plan.notes.push_back(note);
        plan.voices.push_back(voices.empty() ? InstrumentVoice::grand_piano : voices[i]);
    }
    return plan.valid() ? std::optional{std::move(plan)} : std::nullopt;
}
std::optional<SynthRenderResult> recordingGuide(const SynthRenderResult& backing, const OverdubPlan& plan) {
    const auto rate = backing.format.sample_rate_hz;
    const auto channels = backing.format.channel_count;
    if (!plan.valid() || !backing.format.isValid() || rate < 8'000 || rate > 192'000
        || backing.interleaved_samples.size() % channels != 0) { return std::nullopt; }
    const auto lead = static_cast<std::size_t>(std::llround(plan.countInSeconds() * rate));
    const auto take = static_cast<std::size_t>(std::llround(plan.duration * rate));
    if ((lead + take) * channels > 16U * 1024U * 1024U) { return std::nullopt; }
    SynthRenderResult result{backing.format, std::vector<float>((lead + take) * channels),
        static_cast<double>(lead + take) / rate};
    for (std::size_t frame = 0; frame < lead + take; ++frame) {
        const auto click = frame < lead ? metronomeSample(static_cast<double>(frame) / rate, plan.bpm)
            : plan.recording_click ? metronomeSample(plan.origin + static_cast<double>(frame - lead) / rate, plan.bpm) : 0.0F;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto index = frame * channels + channel;
            const auto source = frame >= lead ? (frame - lead) * channels + channel : backing.interleaved_samples.size();
            const auto sample = source < backing.interleaved_samples.size() ? backing.interleaved_samples[source] : 0.0F;
            if (!std::isfinite(sample)) { return std::nullopt; }
            result.interleaved_samples[index] = std::clamp(sample + click, -1.0F, 1.0F);
        }
    }
    return result;
}
}
