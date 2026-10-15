#include "hum/core/simple_synth.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hum::core {

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

[[nodiscard]] std::optional<std::uint64_t> secondsToFrame(
    const double seconds,
    const std::uint32_t sample_rate_hz) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0 || sample_rate_hz == 0) {
        return std::nullopt;
    }
    const auto frames = std::llround(seconds * static_cast<double>(sample_rate_hz));
    if (frames < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(frames);
}

} // namespace

bool SimpleSynthConfig::isValid() const noexcept {
    return sample_rate_hz >= 8'000 && sample_rate_hz <= 384'000
        && channel_count > 0 && channel_count <= 8
        && std::isfinite(master_gain) && master_gain > 0.0F && master_gain <= 1.0F
        && std::isfinite(attack_seconds) && attack_seconds >= 0.0 && attack_seconds <= 1.0
        && std::isfinite(release_seconds) && release_seconds >= 0.0 && release_seconds <= 5.0
        && std::isfinite(maximum_render_seconds)
        && maximum_render_seconds > 0.0 && maximum_render_seconds <= 3'600.0
        && maximum_interleaved_samples > 0;
}

std::uint64_t SynthRenderResult::frameCount() const noexcept {
    return format.channel_count == 0
        ? 0 : interleaved_samples.size() / format.channel_count;
}

double SynthRenderResult::renderedDurationSeconds() const noexcept {
    return format.sample_rate_hz == 0
        ? 0.0 : static_cast<double>(frameCount()) / format.sample_rate_hz;
}

double midiPitchFrequency(const std::uint8_t pitch, const double a4_hz) noexcept {
    if (!std::isfinite(a4_hz) || a4_hz <= 0.0) {
        return 0.0;
    }
    return a4_hz * std::pow(2.0, (static_cast<double>(pitch) - 69.0) / 12.0);
}

std::optional<SynthRenderResult> SimpleSynthRenderer::render(
    const std::span<const TimedMidiNote> notes,
    const SimpleSynthConfig& config) const {
    try {
        if (!config.isValid() || notes.empty()
            || !std::all_of(notes.begin(), notes.end(), [](const auto& note) {
                return note.isValid();
            })) {
            return std::nullopt;
        }

        double phrase_end = 0.0;
        for (const auto& note : notes) {
            phrase_end = std::max(phrase_end, note.start_seconds + note.duration_seconds);
        }
        const auto rendered_end = phrase_end + config.release_seconds;
        if (!std::isfinite(rendered_end) || rendered_end > config.maximum_render_seconds) {
            return std::nullopt;
        }
        const auto frame_count = secondsToFrame(rendered_end, config.sample_rate_hz);
        if (!frame_count || *frame_count == 0
            || *frame_count > std::numeric_limits<std::size_t>::max() / config.channel_count
            || *frame_count * config.channel_count > config.maximum_interleaved_samples) {
            return std::nullopt;
        }

        SynthRenderResult result;
        result.format = {config.sample_rate_hz, config.channel_count};
        result.phrase_duration_seconds = phrase_end;
        result.interleaved_samples.assign(
            static_cast<std::size_t>(*frame_count) * config.channel_count, 0.0F);
        const auto attack_frames = static_cast<std::uint64_t>(std::llround(
            config.attack_seconds * config.sample_rate_hz));
        const auto release_frames = static_cast<std::uint64_t>(std::llround(
            config.release_seconds * config.sample_rate_hz));

        for (const auto& note : notes) {
            const auto start = secondsToFrame(note.start_seconds, config.sample_rate_hz);
            const auto end = secondsToFrame(
                note.start_seconds + note.duration_seconds, config.sample_rate_hz);
            if (!start || !end || *start == std::numeric_limits<std::uint64_t>::max()) {
                return std::nullopt;
            }
            const auto note_end_frame = std::max(*end, *start + 1);
            const auto render_end_frame = std::min<std::uint64_t>(
                *frame_count, note_end_frame + release_frames);
            const auto frequency = midiPitchFrequency(note.pitch);
            const auto velocity_gain = static_cast<double>(note.velocity) / 127.0;
            for (auto frame = *start; frame < render_end_frame; ++frame) {
                const auto age = frame - *start;
                double envelope = attack_frames == 0
                    ? 1.0 : std::min(1.0,
                        static_cast<double>(age + 1) / static_cast<double>(attack_frames));
                if (frame >= note_end_frame) {
                    if (release_frames == 0) {
                        envelope = 0.0;
                    } else {
                        envelope *= std::max(0.0,
                            1.0 - static_cast<double>(frame - note_end_frame + 1)
                                / static_cast<double>(release_frames));
                    }
                }
                const auto phase = kTwoPi * frequency
                    * static_cast<double>(age) / config.sample_rate_hz;
                const auto tone = std::sin(phase) + 0.12 * std::sin(2.0 * phase);
                const auto sample = static_cast<float>(
                    config.master_gain * velocity_gain * envelope * tone);
                const auto base = static_cast<std::size_t>(frame) * config.channel_count;
                for (std::uint16_t channel = 0; channel < config.channel_count; ++channel) {
                    result.interleaved_samples[base + channel] += sample;
                }
            }
        }

        for (auto& sample : result.interleaved_samples) {
            sample = std::clamp(sample, -1.0F, 1.0F);
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace hum::core
