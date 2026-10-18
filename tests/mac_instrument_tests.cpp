#include "MacInstrumentRenderer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace {
int failures = 0;
void check(bool success, const char* message) {
    std::cout << (success ? "PASS " : "FAIL ") << message << '\n';
    if (!success) { ++failures; }
}
double rms(const hum::core::SynthRenderResult& audio, double from, double to) {
    auto start = static_cast<std::size_t>(from * audio.format.sample_rate_hz) * audio.format.channel_count;
    auto stop = std::min(audio.interleaved_samples.size(),
        static_cast<std::size_t>(to * audio.format.sample_rate_hz) * audio.format.channel_count);
    double energy = 0;
    for (auto i = start; i < stop; ++i) { energy += audio.interleaved_samples[i] * audio.interleaved_samples[i]; }
    return stop > start ? std::sqrt(energy / static_cast<double>(stop - start)) : 0;
}
}
int main() {
    using namespace hum::core;
    SimpleSynthConfig config;
    config.release_seconds = 1.2;
    const std::vector<TimedMidiNote> notes{{60,90,0,2,0},{64,90,0,2,0},{67,90,0,2,0}};
    std::string error;
    const auto before = std::chrono::steady_clock::now();
    const auto piano = hum::audio::renderMacInstruments(notes, config, error);
    check(piano.has_value(), error.empty() ? "Mac piano bank loads and renders" : error.c_str());
    config.note_voices.assign(notes.size(), InstrumentVoice::acoustic_guitar);
    const auto guitar = hum::audio::renderMacInstruments(notes, config, error);
    check(guitar.has_value(), error.empty() ? "Mac guitar bank loads and renders" : error.c_str());
    if (!piano || !guitar) { return 1; }
    check(piano->interleaved_samples != guitar->interleaved_samples, "piano and guitar render different sampled voices");
    for (const auto* audio : {&*piano, &*guitar}) {
        check(rms(*audio, .05, .3) > .005, "chord attack is audible");
        for (double t = .3; t < 1.9; t += .1) {
            check(rms(*audio, t, t + .1) > .0001, "held chord has no silent dropouts");
        }
        check(rms(*audio, 2.0, 2.1) > .00001, "note-off has a natural release tail");
        check(std::all_of(audio->interleaved_samples.begin(), audio->interleaved_samples.end(),
            [](float value) { return std::isfinite(value) && std::abs(value) <= .981F; }), "chord mix stays finite without clipping");
        check(std::abs(audio->interleaved_samples.back()) < .00001F, "render ends without a hard cut");
    }
    config.note_voices.clear();
    auto muted = notes;
    for (auto& note : muted) { note.muted = true; }
    const auto silence = hum::audio::renderMacInstruments(muted, config, error);
    check(silence && rms(*silence, 0, 2) < .000001, "muted notes stay silent");
    config.note_voices = {InstrumentVoice::grand_piano};
    check(!hum::audio::renderMacInstruments(notes, config, error), "invalid instrument routing is rejected");
    std::cout << "Instrument test seconds: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count() << '\n';
    return failures ? 1 : 0;
}
