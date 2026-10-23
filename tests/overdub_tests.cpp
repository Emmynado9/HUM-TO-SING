#include "hum/core/overdub.hpp"
#include "hum/core/recording.hpp"
#include "ArmedCapture.hpp"
#include "support/FakeAudioCapture.hpp"
#include <cmath>
#include <iostream>
#include <limits>
int main() {
    using namespace hum::core;
    int failures = 0;
    const auto check = [&](bool value, const char* name) { if (!value) { ++failures; std::cerr << name << '\n'; } };
    const std::vector<TimedMidiNote> notes{{60,90,1,5,0},{64,80,4,2,0},{67,80,4,2,0,true},{70,80,40,1,0}};
    const std::vector<InstrumentVoice> voices{InstrumentVoice::strings,InstrumentVoice::acoustic_guitar,InstrumentVoice::grand_piano,InstrumentVoice::strings};
    for (double bpm : {20., 60., 123., 400.}) {
        const auto plan = makeOverdubPlan(notes, voices, 3.37, bpm, false);
        check(plan && plan->notes.size() == 2, "only audible notes intersecting capture are included");
        if (!plan) { continue; }
        check(plan->notes[0].start_seconds == 0 && std::abs(plan->notes[0].duration_seconds - 2.63) < 1e-8,
            "sustain across the playhead is carried into backing");
        check(plan->voices[1] == InstrumentVoice::acoustic_guitar && std::abs(plan->notes[1].start_seconds - .63) < 1e-8,
            "backing preserves relative offsets and instruments");
        const auto beat = 60 / bpm;
        for (int i = 0; i < 4; ++i) { check(plan->countInBeat((i + .5) * beat) == i + 1, "four count-in beats at requested tempo"); }
        check(plan->countInBeat(4 * beat - .00001) == 4 && plan->countInBeat(4 * beat) == 0,
            "capture starts after all four beats, never at the fourth click");
        check(std::abs(plan->projectPosition(4 * beat + .8) - 4.17) < 1e-8, "project clock excludes count-in");
        auto guide = recordingGuide({{8'000, 1}, {}, 0}, *plan);
        check(guide.has_value(), "guide renders on empty projects too");
        if (!guide) { continue; }
        for (int i = 0; i < 4; ++i) {
            double energy = 0; const auto start = static_cast<std::size_t>(std::llround(i * beat * 8'000));
            for (std::size_t f = start; f < start + 200; ++f) { energy += std::abs(guide->interleaved_samples[f]); }
            check(energy > 1, "every count-in beat is audible even with recording click off");
        }
        const auto lead = static_cast<std::size_t>(std::llround(4 * beat * 8'000));
        check(guide->interleaved_samples.size() == lead + 30 * 8'000, "full recording duration remains after count-in");
        check(std::all_of(guide->interleaved_samples.begin() + lead, guide->interleaved_samples.end(), [](float x) { return x == 0; }),
            "count-in click does not continue when metronome is off");
    }
    auto plan = *makeOverdubPlan({}, {}, 0, 120, true, 1);
    auto guide = recordingGuide({{8'000,1},{},0},plan);
    double energy = 0; for (std::size_t i = 16'000; i < 16'200; ++i) { energy += std::abs(guide->interleaved_samples[i]); }
    check(energy > 1, "optional click continues during recording");
    plan.recording_click = false;
    guide = recordingGuide({{8'000,1},std::vector<float>(8'000,.1F),1},plan);
    check(guide->interleaved_samples[16'000] == .1F && guide->interleaved_samples.back() == .1F,
        "backing begins exactly at count-in boundary");
    check(!makeOverdubPlan(notes, {}, 571, 120, false) && !makeOverdubPlan(notes, {}, 0, 0, false)
        && !makeOverdubPlan(notes, {}, std::numeric_limits<double>::quiet_NaN(), 120, false), "invalid plans rejected");
    hum::test::FakeAudioCapture source; source.device_format = {16'000,1};
    hum::app::ArmedCapture armed(source); RecordingController recorder(armed);
    check(armed.arm("fake-1") == ServiceStatus::ok, "microphone warms before count-in");
    source.emit(std::vector<float>(32'000,.9F));
    check(!recorder.recording(), "count-in audio is not retained");
    check(recorder.start("fake-1") == StartRecordingResult::started && source.start_count == 1,
        "downbeat starts storage without reopening hardware");
    source.emit(std::vector<float>(16'000,.1F));
    check(recorder.stop() && recorder.recording()->sampleCount() == 16'000
        && recorder.recording()->peakLevel() < .2F, "capture excludes every count-in sample");
    check(armed.arm("fake-1") == ServiceStatus::ok, "retry can arm again");
    source.fail(ServiceStatus::device_disconnected);
    check(armed.error() == ServiceStatus::device_disconnected
        && armed.start("fake-1",source.device_format,{}, {}) == ServiceStatus::device_disconnected, "disconnect during count-in cannot start a take");
    armed.stop(); check(!source.isCapturing(), "cancel releases warmed microphone");
    std::cout << "Overdub failures: " << failures << '\n'; return failures ? 1 : 0;
}
