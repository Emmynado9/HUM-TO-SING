#include "hum/vocal/neural_pitch.hpp"
#include "hum/core/wave_fixture.hpp"
#include "hum/core/note_extraction.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: HumVocalFixture <wav> [natural|clean|tight|yin]\n"; return 2; }
    const auto loaded = hum::core::loadWaveFixture(argv[1]);
    if (!loaded) { std::cerr << loaded.error << '\n'; return 1; }
    const auto started = std::chrono::steady_clock::now();
    const auto mode = argc > 2 ? std::string(argv[2]) : "natural";
    std::cout << "pitch,start,duration\n" << std::fixed << std::setprecision(6);
    if (mode == "yin") {
        // Evaluation comparator only; the new app feature never uses this path.
        hum::core::PitchDetectorConfig pitch;
        pitch.minimum_frequency_hz = 55; pitch.maximum_frequency_hz = 1000; pitch.input_gate_rms = .004F;
        pitch.frame_size = 4096; pitch.hop_size = 512;
        const auto result = hum::core::PhraseConversionPipeline(std::make_shared<hum::core::YinPitchDetector>(),
            std::make_shared<hum::core::HysteresisNoteSegmenter>()).convert(*loaded.recording, pitch, {});
        if (result.status == hum::core::PhraseConversionStatus::failed || result.status == hum::core::PhraseConversionStatus::invalid_input) {
            std::cerr << result.message << '\n'; return 1;
        }
        for (const auto& note : result.notes) { std::cout << int(note.pitch) << ',' << note.start_seconds << ',' << note.duration_seconds << '\n'; }
    } else {
        std::ifstream file(HUM_VOCAL_MODEL_FILE, std::ios::binary);
        const std::vector<std::uint8_t> model((std::istreambuf_iterator<char>(file)), {});
        const auto preset = mode == "clean" ? hum::vocal::Preset::clean : mode == "tight" ? hum::vocal::Preset::tight : hum::vocal::Preset::natural;
        const auto result = hum::vocal::infer({loaded.recording->format().sample_rate_hz, loaded.recording->monoSamples()},
            model, hum::vocal::defaultRuntimePath(), hum::vocal::Config::forPreset(preset));
        if (result.status == hum::vocal::Status::failed) { std::cerr << result.error << '\n'; return 1; }
        for (const auto& note : result.notes) { std::cout << note.pitch << ',' << note.start << ',' << note.duration << '\n'; }
        if (argc > 3) {
            std::ofstream frames(argv[3]); frames << "time,pitch,confidence,rms,voiced,onset\n";
            for (const auto& frame : result.frames) {
                frames << frame.time << ',' << frame.midi_pitch << ',' << frame.confidence << ',' << frame.rms << ',' << frame.voiced << ',' << frame.onset << '\n';
            }
        }
        std::cerr << "frames=" << result.frames.size() << " tokens=" << result.tokens.size() << " notes=" << result.notes.size() << ' ';
    }
    std::cerr << "processing_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
}
