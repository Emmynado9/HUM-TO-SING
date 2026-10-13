#pragma once

#include "hum/core/audio.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hum::core {

enum class RecordingState {
    idle,
    starting,
    recording,
    stopping,
    ready,
    error,
    closed,
};

enum class RecordingStopReason {
    none,
    user,
    duration_limit,
    device_failure,
    shutdown,
};

enum class StartRecordingResult {
    started,
    busy,
    permission_required,
    permission_denied,
    device_unavailable,
    invalid_format,
    source_error,
    closed,
};

struct RecordingMetrics {
    std::uint64_t frame_count{0};
    std::uint64_t sample_count{0};
    double duration_seconds{0.0};
    float recent_peak{0.0F};
    float session_peak{0.0F};
    bool clipped{false};
    bool limit_reached{false};
};

class AudioRecording {
public:
    AudioRecording() = default;
    AudioRecording(AudioFormat format, std::vector<float> interleaved_samples);

    [[nodiscard]] const AudioFormat& format() const noexcept;
    [[nodiscard]] const std::vector<float>& interleavedSamples() const noexcept;
    [[nodiscard]] std::uint64_t sampleCount() const noexcept;
    [[nodiscard]] std::uint64_t frameCount() const noexcept;
    [[nodiscard]] double durationSeconds() const noexcept;
    [[nodiscard]] float peakLevel() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool isSilent(float threshold = 0.01F) const noexcept;
    [[nodiscard]] std::vector<float> monoSamples() const;
    [[nodiscard]] std::optional<std::vector<float>> copyMonoFrames(
        std::uint64_t first_frame,
        std::size_t frame_count) const;
    [[nodiscard]] std::optional<AudioRecording> convertedForPlayback(
        std::uint32_t target_sample_rate_hz,
        std::uint16_t target_channel_count) const;

private:
    AudioFormat format_;
    std::vector<float> interleaved_samples_;
};

class RecordingController final {
public:
    explicit RecordingController(
        IAudioCapture& capture,
        double maximum_duration_seconds = 30.0,
        float clipping_threshold = 0.98F);
    ~RecordingController();

    RecordingController(const RecordingController&) = delete;
    RecordingController& operator=(const RecordingController&) = delete;

    [[nodiscard]] StartRecordingResult start(const std::string& device_id);
    [[nodiscard]] bool stop(RecordingStopReason reason = RecordingStopReason::user);
    [[nodiscard]] std::optional<RecordingStopReason> service();
    [[nodiscard]] bool acknowledgeError() noexcept;
    [[nodiscard]] bool clearRecording() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] RecordingState state() const noexcept;
    [[nodiscard]] ServiceStatus lastError() const noexcept;
    [[nodiscard]] RecordingStopReason lastStopReason() const noexcept;
    [[nodiscard]] double maximumDurationSeconds() const noexcept;
    [[nodiscard]] std::optional<AudioFormat> currentFormat() const noexcept;
    [[nodiscard]] RecordingMetrics pollMetrics() noexcept;
    [[nodiscard]] std::shared_ptr<const AudioRecording> recording() const noexcept;
    [[nodiscard]] std::optional<std::vector<float>> copyMonoFrames(
        std::uint64_t first_frame,
        std::size_t frame_count) const;

private:
    struct CallbackState;

    [[nodiscard]] bool finishActiveSession(RecordingStopReason reason, bool failed) noexcept;

    IAudioCapture& capture_;
    double maximum_duration_seconds_;
    float clipping_threshold_;
    RecordingState state_{RecordingState::idle};
    ServiceStatus last_error_{ServiceStatus::ok};
    RecordingStopReason last_stop_reason_{RecordingStopReason::none};
    std::shared_ptr<CallbackState> callback_state_;
    std::shared_ptr<const AudioRecording> recording_;
};

} // namespace hum::core
