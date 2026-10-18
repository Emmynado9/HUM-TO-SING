#pragma once
#include "hum/core/simple_synth.hpp"
#include <string>

namespace hum::audio {
// Offline platform adapter. It receives canonical musical data and has no UI state.
[[nodiscard]] std::optional<core::SynthRenderResult> renderMacInstruments(
    std::span<const core::TimedMidiNote> notes, const core::SimpleSynthConfig& config,
    std::string& error);
}
