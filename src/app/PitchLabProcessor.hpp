#pragma once

#include "hum/core/pitch.hpp"

#include <QObject>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace hum::app {

struct PitchFrameJob {
    std::uint64_t frame_start_sample{0};
    std::vector<float> mono_samples;
};

struct LivePitchBatchResult {
    std::vector<core::RawPitchEstimate> estimates;
    double processing_milliseconds{0.0};
};

struct ReplayPitchResult {
    std::vector<core::DisplayPitchEstimate> estimates;
    double processing_milliseconds{0.0};
};

class PitchLabProcessor final : public QObject {
public:
    using LiveCallback = std::function<void(LivePitchBatchResult)>;
    using ReplayCallback = std::function<void(ReplayPitchResult)>;

    explicit PitchLabProcessor(QObject* parent = nullptr);
    ~PitchLabProcessor() override;

    [[nodiscard]] bool analyze(
        std::vector<PitchFrameJob> frames,
        std::uint32_t sample_rate_hz,
        core::PitchDetectorConfig config,
        LiveCallback callback);
    [[nodiscard]] bool replay(
        std::shared_ptr<const core::AudioRecording> recording,
        core::PitchDetectorConfig config,
        ReplayCallback callback);
    void cancel() noexcept;
    [[nodiscard]] bool isBusy() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace hum::app
