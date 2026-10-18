#pragma once
#include <cmath>
namespace hum::core {
// Playback selection only: it never changes canonical note data.
struct LoopRange {
    double start_seconds{0.0};
    double end_seconds{0.0};
    [[nodiscard]] bool isValid() const noexcept {
        return std::isfinite(start_seconds) && std::isfinite(end_seconds)
            && start_seconds >= 0.0 && end_seconds <= 600.0
            && end_seconds - start_seconds >= .01;
    }
    [[nodiscard]] double wrap(double seconds) const noexcept {
        if (!isValid() || !std::isfinite(seconds)) { return start_seconds; }
        const auto length = end_seconds - start_seconds;
        return start_seconds + std::fmod(std::fmod(seconds - start_seconds, length) + length, length);
    }
    bool operator==(const LoopRange&) const = default;
};
}
