#include "hum/core/voice_clip.hpp"
#include <algorithm>
#include <cmath>

namespace hum::core {
bool VoiceClip::valid() const noexcept {
    return !name.empty() && name.size() <= 256 && std::isfinite(start_seconds) && start_seconds >= 0
        && sample_rate >= 8000 && sample_rate <= 192000 && !samples.empty()
        && duration() <= 30.001 && start_seconds + duration() <= 600;
}
bool validVoiceClips(const std::vector<VoiceClip>& clips) noexcept {
    if (clips.size() > 16) return false;
    std::size_t total = 0;
    for (const auto& clip : clips) {
        if (!clip.valid() || clip.samples.size() > kMaximumVoiceSamples - total) return false;
        total += clip.samples.size();
    }
    return true;
}
std::optional<VoiceClip> makeVoiceClip(const AudioRecording& audio, double origin) {
    if (!audio.isValid()) return std::nullopt;
    VoiceClip clip; clip.start_seconds = origin; clip.sample_rate = audio.format().sample_rate_hz;
    const auto mono = audio.monoSamples(); clip.samples.reserve(mono.size());
    for (auto sample : mono) {
        if (!std::isfinite(sample)) return std::nullopt;
        clip.samples.push_back(static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0F, 1.0F) * 32767)));
    }
    return clip.valid() ? std::optional<VoiceClip>{std::move(clip)} : std::nullopt;
}
bool mixVoiceClips(SynthRenderResult& render, const std::vector<VoiceClip>& clips, double origin, double duration) {
    if (!validVoiceClips(clips) || !render.format.isValid() || render.format.sample_rate_hz > 192000
        || !std::isfinite(render.phrase_duration_seconds) || render.phrase_duration_seconds < 0 || render.phrase_duration_seconds > 600
        || render.interleaved_samples.size() % render.format.channel_count != 0
        || !std::isfinite(origin) || origin < 0 || !std::isfinite(duration) || duration <= 0 || duration > 600) return false;
    const auto rate = render.format.sample_rate_hz;
    const auto channels = render.format.channel_count;
    double end = render.phrase_duration_seconds;
    for (const auto& clip : clips) {
        if (clip.muted || clip.start_seconds >= origin + duration || clip.start_seconds + clip.duration() <= origin) continue;
        end = std::max(end, std::min(duration, clip.start_seconds + clip.duration() - origin));
    }
    const auto frames = static_cast<std::size_t>(std::ceil(end * rate));
    if (frames > 16U * 1024U * 1024U / channels) return false;
    render.interleaved_samples.resize(std::max(render.interleaved_samples.size(), frames * channels), 0);
    for (const auto& clip : clips) {
        if (clip.muted) continue;
        const auto first = static_cast<std::size_t>(std::ceil(std::max(0.0, clip.start_seconds - origin) * rate));
        const auto last = static_cast<std::size_t>(std::ceil(std::max(0.0, std::min(duration, clip.start_seconds + clip.duration() - origin)) * rate));
        for (auto f = first; f < std::min(last, frames); ++f) {
            const auto source = (origin + double(f) / rate - clip.start_seconds) * clip.sample_rate;
            if (source < 0 || source >= double(clip.samples.size())) continue;
            const auto i = static_cast<std::size_t>(source); const auto fraction = source - double(i);
            const auto next = std::min(i + 1, clip.samples.size() - 1);
            const auto value = float((clip.samples[i] * (1 - fraction) + clip.samples[next] * fraction) / 32768.0);
            for (unsigned ch = 0; ch < channels; ++ch) render.interleaved_samples[f * channels + ch] += value;
        }
    }
    render.phrase_duration_seconds = end;
    return true;
}
}
