#pragma once
#include "hum/core/project_file.hpp"
#include <span>

namespace hum::core::trial {
struct Request {
    std::string instrument{"piano"}, style{"neutral"};
    double tempo{120}, duration{0};
    std::uint32_t seed{42};
};
struct Prepared {
    Request request;
    std::vector<std::uint8_t> wave;
    std::string json;
};
[[nodiscard]] bool validInstrument(std::string_view instrument) noexcept;
[[nodiscard]] bool validStyle(std::string_view style) noexcept;
[[nodiscard]] unsigned midiProgram(std::string_view instrument);
[[nodiscard]] std::string prompt(const Request& request);
// Explicit zero-based selection; offset is relative to this voice clip, not the Playlist.
[[nodiscard]] Prepared prepare(const ProjectDocument& project, std::size_t clip,
    Request request, double offset = 0, double duration = 10);
[[nodiscard]] std::vector<std::uint8_t> pcmWave(std::span<const std::int16_t> samples, std::uint32_t rate);
}
