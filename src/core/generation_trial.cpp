#include "hum/core/generation_trial.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace hum::core::trial {
bool validInstrument(std::string_view value) noexcept { return value == "piano" || value == "acoustic-guitar" || value == "flute"; }
bool validStyle(std::string_view value) noexcept {
    return value == "neutral" || value == "gentle" || value == "bright" || value == "melancholic" || value == "energetic";
}
unsigned midiProgram(std::string_view value) {
    if (!validInstrument(value)) throw std::invalid_argument("Choose piano, acoustic-guitar or flute.");
    return value == "flute" ? 73U : value == "acoustic-guitar" ? 24U : 0U;
}
std::string prompt(const Request& r) {
    if (!validInstrument(r.instrument) || !validStyle(r.style) || !std::isfinite(r.tempo) || r.tempo < 20 || r.tempo > 400)
        throw std::invalid_argument("Invalid instrument, style or tempo.");
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "Solo " << (r.instrument == "acoustic-guitar" ? "acoustic guitar" : r.instrument)
        << ", expressive single-note melody, ";
    if (r.style != "neutral") out << r.style << " phrasing, ";
    out << "sparse unaccompanied performance, one melodic line, " << std::setprecision(10) << r.tempo << " BPM.";
    return out.str();
}
std::vector<std::uint8_t> pcmWave(std::span<const std::int16_t> samples, std::uint32_t rate) {
    if (rate < 8000 || rate > 192000 || samples.empty() || samples.size() > static_cast<std::size_t>(rate) * 10)
        throw std::invalid_argument("Trial WAV must contain at most ten seconds at a supported rate.");
    std::vector<std::uint8_t> bytes;
    auto tag = [&](const char* s) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(s[i])); };
    auto u16 = [&](std::uint16_t n) { bytes.push_back(static_cast<std::uint8_t>(n)); bytes.push_back(static_cast<std::uint8_t>(n >> 8)); };
    auto u32 = [&](std::uint32_t n) { u16(static_cast<std::uint16_t>(n)); u16(static_cast<std::uint16_t>(n >> 16)); };
    const auto size = static_cast<std::uint32_t>(samples.size() * 2);
    tag("RIFF"); u32(36 + size); tag("WAVE"); tag("fmt "); u32(16); u16(1); u16(1);
    u32(rate); u32(rate * 2); u16(2); u16(16); tag("data"); u32(size);
    for (auto sample : samples) u16(static_cast<std::uint16_t>(sample));
    return bytes;
}
Prepared prepare(const ProjectDocument& project, std::size_t clip_index, Request request, double offset, double duration) {
    if (!project.isValid() || clip_index >= project.voice_clips.size()) throw std::invalid_argument("Selected voice clip does not exist in a valid project.");
    if (!std::isfinite(offset) || offset < 0 || !std::isfinite(duration) || duration <= 0 || duration > 10)
        throw std::invalid_argument("Offset must be nonnegative; duration must be greater than zero and at most 10 seconds.");
    const auto& clip = project.voice_clips[clip_index];
    if (offset >= clip.duration()) throw std::invalid_argument("Offset is past the selected clip.");
    const auto first = static_cast<std::size_t>(std::floor(offset * clip.sample_rate));
    const auto count = std::min(clip.samples.size() - first, static_cast<std::size_t>(std::floor(duration * clip.sample_rate)));
    if (count < clip.sample_rate / 2) throw std::invalid_argument("Select at least half a second of voice audio.");
    request.tempo = project.tempo_bpm; request.duration = double(count) / clip.sample_rate;
    const auto description = prompt(request);
    const auto samples = std::span<const std::int16_t>(clip.samples).subspan(first, count);
    if (std::none_of(samples.begin(), samples.end(), [](auto s) { return std::abs(int(s)) > 32; }))
        throw std::invalid_argument("The selected excerpt is effectively silent.");
    std::ostringstream json; json.imbue(std::locale::classic()); json << std::setprecision(17);
    json << "{\n  \"schema_version\": 1,\n  \"model\": \"facebook/musicgen-melody\",\n  \"audio_file\": \"guide.wav\",\n"
         << "  \"instrument\": \"" << request.instrument << "\",\n  \"style\": \"" << request.style
         << "\",\n  \"tempo_bpm\": " << request.tempo << ",\n  \"duration_seconds\": " << request.duration
         << ",\n  \"seed\": " << request.seed << ",\n  \"prompt\": \"" << description << "\"\n}\n";
    return {request, pcmWave(samples, clip.sample_rate), json.str()};
}
}
