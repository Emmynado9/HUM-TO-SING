#pragma once

#include "hum/core/recording.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace hum::core {

struct WaveFixtureLoadResult {
    std::optional<AudioRecording> recording;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return recording.has_value();
    }
};

[[nodiscard]] WaveFixtureLoadResult loadWaveFixture(const std::filesystem::path& path);

} // namespace hum::core
