#pragma once
#include "hum/vocal/transcription.hpp"

namespace hum::vocal {
// Native CPU-only inference. It never calls the existing YIN/phrase pipeline.
// Model/runtime are explicit local inputs; this module has no networking code.
[[nodiscard]] ConversionResult infer(const AudioBuffer& audio, std::span<const std::uint8_t> model,
    const std::string& runtime_path, const Config& config,
    CancelCheck cancelled = {}, Progress progress = {});
[[nodiscard]] std::string defaultRuntimePath();
}
