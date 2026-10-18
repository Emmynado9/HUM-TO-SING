#include "MacInstrumentRenderer.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace hum::audio {
namespace {
struct Unit {
    AudioUnit value{};
    ~Unit() { if (value) { AudioUnitUninitialize(value); AudioComponentInstanceDispose(value); } }
};
struct Event {
    std::uint64_t frame;
    std::uint8_t channel, pitch, velocity;
    bool on;
};
}

std::optional<core::SynthRenderResult> renderMacInstruments(
    const std::span<const core::TimedMidiNote> notes, const core::SimpleSynthConfig& config,
    std::string& error) {
    error.clear();
    auto fail = [&](const char* message, OSStatus status = 0) -> std::optional<core::SynthRenderResult> {
        error = message;
        if (status) { error += " (Core Audio " + std::to_string(status) + ")"; }
        return std::nullopt;
    };
    if (!config.isValid() || (!config.note_voices.empty() && config.note_voices.size() != notes.size())
        || (notes.empty() && !config.metronome_enabled)) { return fail("Invalid instrument render configuration."); }
    double end = notes.empty() ? 240.0 / config.tempo_bpm : 0.0;
    for (const auto& note : notes) {
        if (!note.isValid()) { return fail("Invalid instrument note."); }
        end = std::max(end, note.start_seconds + note.duration_seconds);
    }
    const double duration = end + (notes.empty() ? 0.0 : config.release_seconds);
    if (!std::isfinite(duration) || duration > config.maximum_render_seconds
        || duration * config.sample_rate_hz * config.channel_count > config.maximum_interleaved_samples) {
        return fail("The instrument arrangement exceeds the render limit.");
    }
    const auto frames = static_cast<std::uint64_t>(std::llround(duration * config.sample_rate_hz));
    if (!frames) { return fail("The instrument arrangement is empty."); }
    try {
        Unit synth;
        AudioComponentDescription description{kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth,
            kAudioUnitManufacturer_Apple, 0, 0};
        const auto component = AudioComponentFindNext(nullptr, &description);
        if (!component) { return fail("The Mac sampled instrument is unavailable."); }
        auto status = AudioComponentInstanceNew(component, &synth.value);
        if (status) { return fail("Could not create the Mac sampled instrument.", status); }
        AudioStreamBasicDescription format{};
        format.mSampleRate = config.sample_rate_hz;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked)
            | static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
        format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float);
        format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = 2;
        format.mBitsPerChannel = 32;
        status = AudioUnitSetProperty(synth.value, kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Output, 0, &format, sizeof(format));
        if (status) { return fail("Could not configure the sampled instrument output.", status); }
        UInt32 block_size = 512;
        status = AudioUnitSetProperty(synth.value, kAudioUnitProperty_MaximumFramesPerSlice,
            kAudioUnitScope_Global, 0, &block_size, sizeof(block_size));
        if (status) { return fail("Could not configure instrument buffering.", status); }
        status = AudioUnitInitialize(synth.value);
        if (status) { return fail("Could not load the Mac instrument bank.", status); }
        for (std::uint8_t channel = 0; channel < 5; ++channel) {
            status = MusicDeviceMIDIEvent(synth.value, 0xc0U | channel,
                core::instrumentVoiceMidiProgram(static_cast<core::InstrumentVoice>(channel)), 0, 0);
            if (status) { return fail("Could not select the instrument.", status); }
        }
        std::vector<Event> events;
        events.reserve(notes.size() * 2);
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const auto& note = notes[i];
            if (note.muted) { continue; }
            const auto channel = static_cast<std::uint8_t>(config.note_voices.empty()
                ? core::InstrumentVoice::grand_piano : config.note_voices[i]);
            const auto start = static_cast<std::uint64_t>(std::llround(note.start_seconds * config.sample_rate_hz));
            const auto stop = std::max(start + 1, static_cast<std::uint64_t>(
                std::llround((note.start_seconds + note.duration_seconds) * config.sample_rate_hz)));
            events.push_back({start, channel, note.pitch, note.velocity, true});
            events.push_back({stop, channel, note.pitch, 0, false});
        }
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.frame == b.frame ? a.on < b.on : a.frame < b.frame;
        });
        core::SynthRenderResult result{{config.sample_rate_hz, config.channel_count},
            std::vector<float>(static_cast<std::size_t>(frames) * config.channel_count), end};
        std::array<std::array<unsigned, 128>, 5> held{};
        std::array<float, 512> left{}, right{};
        struct StereoBuffers { UInt32 count; AudioBuffer buffers[2]; } buffers{
            2, {{1, sizeof(left), left.data()}, {1, sizeof(right), right.data()}}};
        std::size_t cursor = 0;
        for (std::uint64_t frame = 0; frame < frames; frame += block_size) {
            const auto count = static_cast<UInt32>(std::min<std::uint64_t>(block_size, frames - frame));
            while (cursor < events.size() && events[cursor].frame < frame + count) {
                const auto& event = events[cursor++];
                auto& active = held[event.channel][event.pitch];
                if (event.on) { ++active; }
                else if (active && --active) { continue; }
                status = MusicDeviceMIDIEvent(synth.value, (event.on ? 0x90U : 0x80U) | event.channel,
                    event.pitch, event.velocity, static_cast<UInt32>(event.frame - frame));
                if (status) { return fail("Could not schedule an instrument note.", status); }
            }
            AudioTimeStamp time{};
            time.mFlags = kAudioTimeStampSampleTimeValid;
            time.mSampleTime = static_cast<Float64>(frame);
            AudioUnitRenderActionFlags flags = 0;
            for (auto& buffer : buffers.buffers) { buffer.mDataByteSize = count * sizeof(float); }
            status = AudioUnitRender(synth.value, &flags, &time, 0, count,
                reinterpret_cast<AudioBufferList*>(&buffers));
            if (status) { return fail("Could not render the sampled instrument.", status); }
            for (UInt32 i = 0; i < count; ++i) {
                const auto click = config.metronome_enabled
                    ? core::metronomeSample(static_cast<double>(frame + i) / config.sample_rate_hz, config.tempo_bpm) : 0.0F;
                // Retain the natural note-off tails; fade only the last 20 ms of the rendered buffer.
                const auto fade = notes.empty() ? 1.0F : static_cast<float>(std::min(1.0,
                    static_cast<double>(frames - frame - i - 1) / (config.sample_rate_hz * .02)));
                for (std::uint16_t channel = 0; channel < config.channel_count; ++channel) {
                    const auto sample = config.channel_count == 1 ? (left[i] + right[i]) * .5F
                        : (channel % 2 == 0 ? left[i] : right[i]);
                    result.interleaved_samples[(frame + i) * config.channel_count + channel] =
                        (sample * (config.master_gain / .20F) + click) * fade;
                }
            }
        }
        float peak = 1.0F;
        for (const auto sample : result.interleaved_samples) {
            if (!std::isfinite(sample)) { return fail("Instrument produced invalid audio."); }
            peak = std::max(peak, std::abs(sample));
        }
        // One constant gain for the complete mix avoids clipping and gain pumping on chords.
        if (peak > .98F) {
            for (auto& sample : result.interleaved_samples) { sample *= .98F / peak; }
        }
        return result;
    } catch (...) { return fail("Could not allocate the sampled instrument buffer."); }
}
}
