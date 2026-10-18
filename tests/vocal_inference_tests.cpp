#include "hum/vocal/neural_pitch.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <random>

int main(int argc, char** argv) {
    using namespace hum::vocal;
    int failures = 0;
    const auto expect = [&](bool value, const char* message) { if (!value) { ++failures; std::cerr << message << '\n'; } };
    std::ifstream file(HUM_VOCAL_MODEL_FILE, std::ios::binary);
    const std::vector<std::uint8_t> model((std::istreambuf_iterator<char>(file)), {});
    if (argc > 1 && std::string(argv[1]) == "--missing-runtime") {
        const auto result = infer({16'000, std::vector<float>(16'000, .1F)}, model, "/missing-vocal-runtime", {});
        expect(result.status == Status::failed && !result.error.empty() && result.notes.empty(), "missing runtime reports failure without old-engine fallback");
        return failures ? 1 : 0;
    }
    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t rate : {16'000U, 44'100U, 48'000U}) {
        AudioBuffer audio{rate, std::vector<float>(rate * 3)};
        for (std::size_t i = rate / 5; i < audio.samples.size() - rate / 5; ++i) {
            const auto t = static_cast<double>(i) / rate;
            audio.samples[i] = static_cast<float>(.2 * std::sin(2 * std::numbers::pi * 220 * t)
                + .08 * std::sin(4 * std::numbers::pi * 220 * t));
        }
        const auto result = infer(audio, model, defaultRuntimePath(), {});
        std::cout << rate << " Hz: notes=" << result.notes.size() << " error=" << result.error << '\n';
        expect(result.status == Status::success && result.notes.size() == 1, "neural sustained hum stays one note across sample rates and chunk boundary");
        if (!result.notes.empty()) {
            expect(result.notes[0].pitch == 57 && result.notes[0].duration > 2.3, "neural pitch and duration close to known A3 fixture");
        }
        expect(result.frames.size() == 187, "normalized neural timestamps preserve actual duration");
    }
    AudioBuffer silence{16'000, std::vector<float>(16'000)};
    expect(infer(silence, model, defaultRuntimePath(), {}).notes.empty(), "neural silence has no spurious notes");
    std::mt19937 rng(41); std::uniform_real_distribution<float> noise(-.01F, .01F);
    for (auto& sample : silence.samples) { sample = noise(rng); }
    expect(infer(silence, model, defaultRuntimePath(), {}).notes.empty(), "low-level breath-like noise has no note storm");
    expect(infer(silence, model, defaultRuntimePath(), {}, [] { return true; }).status == Status::cancelled, "backend cancellation supported");
    expect(infer(silence, {}, defaultRuntimePath(), {}).status == Status::failed, "missing model produces explicit failure");
    const std::vector<std::uint8_t> corrupt{1, 2, 3, 4};
    expect(infer(silence, corrupt, defaultRuntimePath(), {}).status == Status::failed, "corrupt model fails cleanly");
    AudioBuffer repeated{16'000, std::vector<float>(32'000)};
    for (std::size_t i = 0; i < repeated.samples.size(); ++i) {
        const auto t = static_cast<double>(i) / 16'000;
        if ((t >= .2 && t < .9) || (t >= 1.02 && t < 1.8)) {
            repeated.samples[i] = static_cast<float>(.3 * std::sin(2 * std::numbers::pi * 220 * t));
        }
    }
    const auto repeated_result = infer(repeated, model, defaultRuntimePath(), {});
    for (const auto& note : repeated_result.notes) { std::cout << "Repeated fixture: " << note.pitch << ' ' << note.start << ' ' << note.duration << '\n'; }
    expect(repeated_result.notes.size() == 2, "actual neural repeated articulation preserves two same-pitch notes");
    for (auto& sample : repeated.samples) { sample = std::clamp(sample * 8, -1.0F, 1.0F); }
    const auto clipped = infer(repeated, model, defaultRuntimePath(), {});
    expect(!clipped.warnings.empty() && clipped.notes.size() <= 4, "clipped input reports a warning without a note storm");
    AudioBuffer long_audio{16'000, std::vector<float>(48'000, .1F)};
    bool cancel_after_batch = false;
    expect(infer(long_audio, model, defaultRuntimePath(), {}, [&] { return cancel_after_batch; },
        [&](double fraction) { cancel_after_batch = fraction > .2; }).status == Status::cancelled, "cancellation between neural batches is respected");
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::cout << "Neural fixture suite: " << ms << " ms; failures=" << failures << '\n';
    return failures ? 1 : 0;
}
