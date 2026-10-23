#include "hum/core/generation_trial.hpp"
#include "hum/core/midi_file.hpp"
#include "hum/core/simple_synth.hpp"
#include "hum/core/wave_fixture.hpp"
#include "hum/vocal/neural_pitch.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
std::string read(const fs::path& path, std::uintmax_t limit) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > limit) throw std::runtime_error("Missing or oversized input: " + path.string());
    std::ifstream in(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)), {});
    if (!in || data.size() != fs::file_size(path)) throw std::runtime_error("Could not read input.");
    return data;
}
void write(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary); out.write(data.data(), static_cast<std::streamsize>(data.size())); out.close();
    if (!out) throw std::runtime_error("Could not write output: " + path.string());
}
void write(const fs::path& path, const std::vector<std::uint8_t>& data) { write(path, std::string(data.begin(), data.end())); }
struct Output {
    fs::path path; bool committed{false};
    explicit Output(fs::path p) : path(std::move(p)) {
        if (fs::exists(path)) throw std::runtime_error("Output already exists; choose a new directory to keep each trial separate.");
        if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
        if (!fs::create_directory(path)) throw std::runtime_error("Could not create trial directory.");
    }
    ~Output() { if (!committed) { std::error_code ignored; fs::remove_all(path, ignored); } }
};
std::uint32_t integer(const std::string& s) {
    std::uint32_t value = 0; const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || end != s.data() + s.size()) throw std::invalid_argument("Expected an unsigned integer: " + s);
    return value;
}
double number(const std::string& s) {
    std::size_t end = 0; const auto value = std::stod(s, &end);
    if (end != s.size() || !std::isfinite(value)) throw std::invalid_argument("Expected a finite number: " + s);
    return value;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
            std::cout << "HumGenerationTrial prepare --project song.humproj --clip 1 --instrument piano|acoustic-guitar|flute --output NEW_DIR [--style neutral|gentle|bright|melancholic|energetic] [--seed 42] [--offset 0] [--duration 10]\n"
                << "HumGenerationTrial list --project song.humproj\n"
                << "HumGenerationTrial transcribe --audio generated.wav --bpm 120 --instrument piano|acoustic-guitar|flute --output NEW_DIR\n"
                << "Clip numbers are one-based. No network requests. Existing output directories are never overwritten.\n";
            return argc == 1 ? 2 : 0;
        }
        const std::string command = argv[1];
        const std::vector<std::string> allowed = command == "prepare" ? std::vector<std::string>{"--project","--clip","--instrument","--output","--style","--seed","--offset","--duration"}
            : command == "list" ? std::vector<std::string>{"--project"}
            : command == "transcribe" ? std::vector<std::string>{"--audio","--bpm","--instrument","--output"} : std::vector<std::string>{};
        if (allowed.empty()) throw std::invalid_argument("Unknown command; use --help.");
        std::map<std::string,std::string> args;
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc || std::find(allowed.begin(), allowed.end(), argv[i]) == allowed.end() || !args.emplace(argv[i], argv[i + 1]).second)
                throw std::invalid_argument("Unknown, duplicate or incomplete option.");
        }
        auto required = [&](const std::string& key) { if (!args.contains(key)) throw std::invalid_argument("Missing " + key); return args.at(key); };
        auto optional = [&](const std::string& key, const std::string& fallback) { return args.contains(key) ? args.at(key) : fallback; };
        if (command == "prepare" || command == "list") {
            const auto bytes = read(required("--project"), 64U * 1024U * 1024U);
            const auto project = hum::core::ProjectFileCodec::decode(bytes);
            if (!project) throw std::runtime_error(project.message);
            if (command == "list") {
                for (std::size_t i = 0; i < project.project->voice_clips.size(); ++i) {
                    const auto& clip = project.project->voice_clips[i];
                    std::cout << i + 1 << '\t' << std::quoted(clip.name) << '\t' << clip.duration() << " seconds\n";
                }
                if (project.project->voice_clips.empty()) std::cout << "No saved voice clips. Use Record voice and Save project first.\n";
                return 0;
            }
            const auto index = integer(required("--clip"));
            if (index == 0) throw std::invalid_argument("Clip numbers start at 1.");
            hum::core::trial::Request request;
            request.instrument = required("--instrument"); request.style = optional("--style", "neutral"); request.seed = integer(optional("--seed", "42"));
            const auto prepared = hum::core::trial::prepare(*project.project, index - 1, request,
                number(optional("--offset", "0")), number(optional("--duration", "10")));
            Output out(required("--output")); write(out.path / "guide.wav", prepared.wave); write(out.path / "request.json", prepared.json);
            out.committed = true;
            std::cout << "Prepared " << prepared.request.duration << " seconds locally. Nothing uploaded.\n" << prepared.json;
            return 0;
        }
#ifndef HUM_TRIAL_NEURAL
        throw std::runtime_error("Neural runtime unavailable. Configure HUM_ONNXRUNTIME_ROOT; no detector substitution is performed.");
#else
        const auto audio = required("--audio"), destination = required("--output"), instrument = required("--instrument");
        const auto bpm = number(required("--bpm"));
        if (bpm < 20 || bpm > 400) throw std::invalid_argument("BPM must be 20 to 400.");
        const auto program = hum::core::trial::midiProgram(instrument);
        if (!fs::is_regular_file(audio) || fs::file_size(audio) > 16U * 1024U * 1024U) throw std::runtime_error("Missing or oversized WAV.");
        if (fs::exists(destination)) throw std::runtime_error("Output already exists; choose a new directory.");
        const auto loaded = hum::core::loadWaveFixture(audio);
        if (!loaded) throw std::runtime_error(loaded.error);
        if (loaded.recording->durationSeconds() < .5 || loaded.recording->durationSeconds() > 10.001) throw std::runtime_error("Generated trial audio must be 0.5 to 10 seconds.");
        const auto model_data = read(HUM_VOCAL_MODEL_FILE, 1024U * 1024U);
        const std::vector<std::uint8_t> model(model_data.begin(), model_data.end());
        const auto started = std::chrono::steady_clock::now();
        const auto result = hum::vocal::infer({loaded.recording->format().sample_rate_hz, loaded.recording->monoSamples()},
            model, hum::vocal::defaultRuntimePath(), hum::vocal::Config::forPreset(hum::vocal::Preset::natural));
        if (result.status != hum::vocal::Status::success || result.notes.empty()) throw std::runtime_error("No usable neural transcription: " + result.error);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::vector<hum::core::TimedMidiNote> notes;
        std::ostringstream csv; csv << "pitch,start_seconds,duration_seconds,velocity\n" << std::setprecision(12);
        for (const auto& note : result.notes) {
            notes.push_back({static_cast<std::uint8_t>(note.pitch), static_cast<std::uint8_t>(note.velocity), note.start, note.duration, 0});
            csv << note.pitch << ',' << note.start << ',' << note.duration << ',' << note.velocity << '\n';
        }
        auto sequence = hum::core::MidiTimeline::fromTimedNotes(notes, bpm);
        if (!sequence) throw std::runtime_error("Detected notes cannot be serialized safely.");
        sequence->program_changes = {{0, static_cast<std::uint8_t>(program)}};
        const auto midi = hum::core::StandardMidiFileExporter::serialize(*sequence);
        if (!midi) throw std::runtime_error("MIDI export failed.");
        hum::core::SimpleSynthConfig synth; synth.channel_count = 1; synth.sample_rate_hz = 32000; synth.release_seconds = 0;
        const auto preview = hum::core::SimpleSynthRenderer{}.render(notes, synth);
        if (!preview) throw std::runtime_error("Reference audio render failed.");
        std::vector<std::int16_t> pcm; pcm.reserve(preview->interleaved_samples.size());
        for (auto sample : preview->interleaved_samples) pcm.push_back(static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0F, 1.0F) * 32767)));
        Output out(destination); write(out.path / "generated.mid", *midi); write(out.path / "notes.csv", csv.str());
        write(out.path / "midi-reference.wav", hum::core::trial::pcmWave(pcm, 32000));
        std::ostringstream metadata; metadata << std::setprecision(12)
            << "{\"engine\":\"swift-f0\",\"preset\":\"natural\",\"instrument\":\"" << instrument << "\",\"tempo_bpm\":" << bpm
            << ",\"note_count\":" << notes.size() << ",\"processing_seconds\":" << elapsed
            << ",\"reference_timbre\":\"generic built-in tone, not the generated instrument\",\"quality\":\"unrated\"}\n";
        write(out.path / "transcription.json", metadata.str());
        out.committed = true;
        std::cout << "Exported " << notes.size() << " notes. Listen and assess; instrument transcription accuracy is unverified.\n";
        for (const auto& warning : result.warnings) std::cerr << warning << '\n';
        return 0;
#endif
    } catch (const std::exception& error) { std::cerr << "Trial stopped: " << error.what() << '\n'; return 1; }
}
