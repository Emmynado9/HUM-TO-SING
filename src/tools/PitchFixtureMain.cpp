#include "hum/core/pitch.hpp"
#include "hum/core/wave_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace {

void printUsage() {
    std::cout
        << "Usage: HumPitchFixture <fixture.wav> [frame-size] [hop-size] [gate-rms] [min-hz] [max-hz]\n"
        << "Supported input: PCM 16-bit or IEEE-float 32-bit RIFF/WAVE.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string{argv[1]} == "--help") {
        printUsage();
        return argc < 2 ? 2 : 0;
    }

    hum::core::PitchDetectorConfig config;
    try {
        if (argc > 2) {
            config.frame_size = static_cast<std::size_t>(std::stoull(argv[2]));
        }
        if (argc > 3) {
            config.hop_size = static_cast<std::size_t>(std::stoull(argv[3]));
        }
        if (argc > 4) {
            config.input_gate_rms = std::stof(argv[4]);
        }
        if (argc > 5) {
            config.minimum_frequency_hz = std::stod(argv[5]);
        }
        if (argc > 6) {
            config.maximum_frequency_hz = std::stod(argv[6]);
        }
    } catch (const std::exception&) {
        std::cerr << "Invalid numeric Pitch Lab setting.\n";
        return 2;
    }

    const auto loaded = hum::core::loadWaveFixture(argv[1]);
    if (!loaded) {
        std::cerr << loaded.error << '\n';
        return 1;
    }
    const auto& recording = *loaded.recording;
    if (!config.isValid(recording.format().sample_rate_hz)) {
        std::cerr << "Pitch settings are invalid for the fixture sample rate. Increase the frame size or narrow the range.\n";
        return 2;
    }

    const hum::core::PitchAnalysisRunner runner(
        std::make_shared<hum::core::YinPitchDetector>());
    const auto results = runner.analyze(recording, config);
    std::cout << "time_seconds,raw_hz,display_hz,note,cents,voicing_quality,rms,status\n";
    std::size_t reliable_count = 0;
    for (const auto& result : results) {
        const auto center_sample = static_cast<double>(result.raw.frame_start_sample)
            + static_cast<double>(config.frame_size) / 2.0;
        const auto time_seconds = center_sample / recording.format().sample_rate_hz;
        std::cout << std::fixed << std::setprecision(6) << time_seconds << ','
                  << result.raw.frequency_hz << ',';
        if (result.reliable) {
            ++reliable_count;
            std::cout << result.frequency_hz << ',' << result.note_name << ','
                      << result.cents_deviation;
        } else {
            std::cout << ",No reliable pitch,";
        }
        std::cout << ',' << result.raw.voicing_quality << ',' << result.raw.rms_level << ','
                  << hum::core::pitchStatusText(result.raw.status) << '\n';
    }
    std::cerr << "Analyzed " << results.size() << " frames; " << reliable_count
              << " reliable pitch frames.\n";
    return 0;
}
