#include "hum/vocal/transcription.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace hum::vocal;
int main() {
    int failures = 0;
    const auto expect = [&](bool value, const char* message) { if (!value) { ++failures; std::cerr << message << '\n'; } };
    const auto fixture = [](double pitch, double duration, double step = .01) {
        std::vector<PitchFrame> frames;
        for (int i = 0; i * step < duration - 1e-8; ++i) { frames.push_back({i * step, pitch, .95, .1, true}); }
        return frames;
    };
    auto frames = fixture(60, 2);
    auto result = transcribe(frames, 2, {});
    expect(result.status == Status::success && result.notes.size() == 1 && result.notes[0].pitch == 60, "stable sustain produces one note");
    expect(result.notes[0].duration > 1.98, "sustain duration retained");
    expect(reconstruct(result.tokens) == result.notes, "transparent tokens reconstruct exactly the preview notes");
    const std::vector<MusicalToken> slide{{TokenType::note_start, 1, 58}, {TokenType::pitch_slide, 1.2, 58, 40}, {TokenType::note_end, 1.8, 58}};
    const auto decoded_slide = reconstruct(slide);
    expect(decoded_slide.size() == 1 && decoded_slide[0].pitch == 58 && std::abs(decoded_slide[0].duration - .8) < 1e-9,
        "slides decode to documented fixed-pitch fallback");
    for (std::size_t i = 50; i < 90; ++i) { frames[i].confidence = .49; }
    expect(transcribe(frames, 2, {}).notes.size() == 1, "confidence near the onset threshold cannot chop an established pitch");
    frames[100].onset = true;
    result = transcribe(frames, 2, {});
    expect(result.notes.size() == 2, "clear same-pitch re-onset remains two notes");
    for (std::size_t i = 0; i < frames.size(); ++i) { frames[i].onset = false; frames[i].midi_pitch = 60 + .45 * std::sin(i * .31); }
    result = transcribe(frames, 2, {});
    expect(result.notes.size() == 1 && result.notes[0].pitch == 60, "vibrato stays a sustained note");
    expect(std::any_of(result.tokens.begin(), result.tokens.end(), [](auto t) { return t.type == TokenType::pitch_slide; }), "expressive contour has transparent slide tokens");
    frames = fixture(60, .8);
    for (std::size_t i = 0; i < frames.size(); ++i) { frames[i].midi_pitch = 60 + (i / 10) % 4; }
    result = transcribe(frames, .8, {});
    expect(result.notes.size() == 8, "intentional 100 ms notes remain distinct");
    for (auto& f : frames) { f.rms = 0; }
    expect(transcribe(frames, .8, {}).notes.empty(), "silence never emits a note storm");
    for (auto& f : frames) { f.rms = .1; f.confidence = .1; }
    expect(transcribe(frames, .8, {}).notes.empty(), "low-confidence breath/noise suppressed");
    for (double step : {.005, .01, .02}) {
        frames = fixture(60, 2, step);
        for (auto& f : frames) { if (f.time >= .8 && f.time < .83 - 1e-9) { f.confidence = .1; } }
        expect(transcribe(frames, 2, {}).notes.size() == 1, "time-based audible gap bridge independent of frame rate");
        for (auto& f : frames) { if (f.time >= .8 && f.time < .83 - 1e-9) { f.rms = 0; } }
        expect(transcribe(frames, 2, {}).notes.size() == 2, "intentional short silent rest is not bridged");
    }
    frames = fixture(60, 2);
    frames[20].time = frames[19].time;
    expect(transcribe(frames, 2, {}).status == Status::failed, "duplicate frame times rejected");
    Config invalid; invalid.sensitivity = std::numeric_limits<double>::quiet_NaN();
    expect(transcribe(fixture(60, 2), 2, invalid).status == Status::failed, "invalid threshold rejected");
    expect(transcribe(fixture(60, 2), 2, {}, [] { return true; }).status == Status::cancelled, "cancellation is explicit");
    const std::vector<NoteEvent> chromatic{{61, .13, .31, 96}, {63, .6, .2, 96}};
    expect(correct(chromatic, {}) == chromatic, "Natural preserves original chromatic notes and timing");
    Correction correction; correction.scale_snap = true;
    auto corrected = correct(chromatic, correction);
    expect(corrected[0].pitch == 60 && corrected[1].pitch == 62, "scale ties choose lower pitch deterministically");
    expect(chromatic[0].pitch == 61, "correction does not overwrite original transcription");
    for (double bpm : {60., 123., 240.}) {
        correction = {}; correction.bpm = bpm; correction.origin = 1.37; correction.quantize_division = 8;
        corrected = correct(chromatic, correction);
        const auto grid = 30 / bpm;
        expect(std::abs((corrected[0].start + correction.origin) / grid - std::round((corrected[0].start + correction.origin) / grid)) < 1e-8,
            "quantization uses absolute origin and actual BPM");
        expect(corrected[0].duration > 0 && corrected[0].start + corrected[0].duration <= corrected[1].start + 1e-9, "quantized notes remain valid and monophonic");
    }
    correction = {}; correction.quantize_division = 4;
    corrected = correct({chromatic.data(), chromatic.size()}, correction);
    expect(corrected.size() == 2, "coarse correction never drops the phrase");
    std::cout << "Vocal transcription failures: " << failures << '\n'; return failures ? 1 : 0;
}
