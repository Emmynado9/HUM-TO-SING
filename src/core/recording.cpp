#include "hum/core/recording.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hum::core {

namespace {

class BoundedCaptureBuffer final {
public:
    BoundedCaptureBuffer(
        const AudioFormat format,
        const double maximum_duration_seconds,
        const float clipping_threshold)
        : format_(format),
          clipping_threshold_(clipping_threshold) {
        const long double requested_frames = std::floor(
            static_cast<long double>(format.sample_rate_hz) * maximum_duration_seconds);
        const long double requested_samples = requested_frames * format.channel_count;
        if (requested_samples <= 0.0L
            || requested_samples > static_cast<long double>(std::numeric_limits<std::size_t>::max())) {
            throw std::length_error("recording buffer size is not representable");
        }
        samples_.resize(static_cast<std::size_t>(requested_samples));
    }

    [[nodiscard]] bool append(const AudioBufferView audio) noexcept {
        if (!audio.isValid()
            || audio.sample_rate_hz != format_.sample_rate_hz
            || audio.channel_count != format_.channel_count) {
            return false;
        }

        const auto position = samples_written_.load(std::memory_order_relaxed);
        const auto available = samples_.size() - std::min(position, samples_.size());
        const auto count = std::min(available, audio.interleaved_samples.size());
        float block_peak = 0.0F;

        if (count > 0) {
            std::memcpy(
                samples_.data() + position,
                audio.interleaved_samples.data(),
                count * sizeof(float));
            for (std::size_t index = 0; index < count; ++index) {
                block_peak = std::max(block_peak, std::abs(audio.interleaved_samples[index]));
            }
            samples_written_.store(position + count, std::memory_order_release);
        }

        recent_peak_.store(block_peak, std::memory_order_relaxed);
        auto previous_peak = session_peak_.load(std::memory_order_relaxed);
        while (block_peak > previous_peak
            && !session_peak_.compare_exchange_weak(
                previous_peak, block_peak, std::memory_order_relaxed)) {
        }
        if (block_peak >= clipping_threshold_) {
            clipped_.store(true, std::memory_order_relaxed);
        }
        if (count < audio.interleaved_samples.size() || position + count >= samples_.size()) {
            limit_reached_.store(true, std::memory_order_release);
        }
        return true;
    }

    [[nodiscard]] RecordingMetrics pollMetrics() noexcept {
        const auto sample_count = samples_written_.load(std::memory_order_acquire);
        const auto frame_count = sample_count / format_.channel_count;
        return {
            static_cast<std::uint64_t>(frame_count),
            static_cast<std::uint64_t>(sample_count),
            static_cast<double>(frame_count) / static_cast<double>(format_.sample_rate_hz),
            recent_peak_.exchange(0.0F, std::memory_order_relaxed),
            session_peak_.load(std::memory_order_relaxed),
            clipped_.load(std::memory_order_relaxed),
            limit_reached_.load(std::memory_order_acquire),
        };
    }

    [[nodiscard]] std::vector<float> snapshot() const {
        const auto count = samples_written_.load(std::memory_order_acquire);
        return {samples_.cbegin(), samples_.cbegin() + static_cast<std::ptrdiff_t>(count)};
    }

    [[nodiscard]] std::optional<std::vector<float>> copyMonoFrames(
        const std::uint64_t first_frame,
        const std::size_t frame_count) const {
        const auto published_samples = samples_written_.load(std::memory_order_acquire);
        const auto published_frames = published_samples / format_.channel_count;
        if (first_frame > published_frames
            || frame_count > published_frames - static_cast<std::size_t>(first_frame)) {
            return std::nullopt;
        }

        std::vector<float> mono(frame_count);
        const auto first = static_cast<std::size_t>(first_frame);
        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            float sum = 0.0F;
            for (std::uint16_t channel = 0; channel < format_.channel_count; ++channel) {
                sum += samples_[(first + frame) * format_.channel_count + channel];
            }
            mono[frame] = sum / static_cast<float>(format_.channel_count);
        }
        return mono;
    }

    [[nodiscard]] const AudioFormat& format() const noexcept {
        return format_;
    }

    [[nodiscard]] bool limitReached() const noexcept {
        return limit_reached_.load(std::memory_order_acquire);
    }

private:
    AudioFormat format_;
    float clipping_threshold_;
    std::vector<float> samples_;
    std::atomic<std::size_t> samples_written_{0};
    std::atomic<float> recent_peak_{0.0F};
    std::atomic<float> session_peak_{0.0F};
    std::atomic<bool> clipped_{false};
    std::atomic<bool> limit_reached_{false};
};

[[nodiscard]] StartRecordingResult startResultFor(const ServiceStatus status) noexcept {
    switch (status) {
    case ServiceStatus::ok:
        return StartRecordingResult::started;
    case ServiceStatus::busy:
        return StartRecordingResult::busy;
    case ServiceStatus::permission_required:
        return StartRecordingResult::permission_required;
    case ServiceStatus::permission_denied:
        return StartRecordingResult::permission_denied;
    case ServiceStatus::unavailable:
    case ServiceStatus::device_disconnected:
        return StartRecordingResult::device_unavailable;
    case ServiceStatus::invalid_input:
        return StartRecordingResult::invalid_format;
    case ServiceStatus::io_error:
        return StartRecordingResult::source_error;
    }
    return StartRecordingResult::source_error;
}

} // namespace

struct RecordingController::CallbackState {
    explicit CallbackState(std::shared_ptr<BoundedCaptureBuffer> capture_buffer)
        : buffer(std::move(capture_buffer)) {
    }

    std::shared_ptr<BoundedCaptureBuffer> buffer;
    std::atomic<ServiceStatus> failure{ServiceStatus::ok};
};

AudioRecording::AudioRecording(
    const AudioFormat format,
    std::vector<float> interleaved_samples)
    : format_(format),
      interleaved_samples_(std::move(interleaved_samples)) {
}

const AudioFormat& AudioRecording::format() const noexcept {
    return format_;
}

const std::vector<float>& AudioRecording::interleavedSamples() const noexcept {
    return interleaved_samples_;
}

std::uint64_t AudioRecording::sampleCount() const noexcept {
    return static_cast<std::uint64_t>(interleaved_samples_.size());
}

std::uint64_t AudioRecording::frameCount() const noexcept {
    if (!format_.isValid()) {
        return 0;
    }
    return static_cast<std::uint64_t>(interleaved_samples_.size() / format_.channel_count);
}

double AudioRecording::durationSeconds() const noexcept {
    if (!format_.isValid()) {
        return 0.0;
    }
    return static_cast<double>(frameCount()) / static_cast<double>(format_.sample_rate_hz);
}

float AudioRecording::peakLevel() const noexcept {
    float peak = 0.0F;
    for (const auto sample : interleaved_samples_) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

bool AudioRecording::isValid() const noexcept {
    return format_.isValid()
        && interleaved_samples_.size() % format_.channel_count == 0;
}

bool AudioRecording::isSilent(const float threshold) const noexcept {
    return peakLevel() < std::max(threshold, 0.0F);
}

std::vector<float> AudioRecording::monoSamples() const {
    auto mono = copyMonoFrames(0, static_cast<std::size_t>(frameCount()));
    if (!mono) {
        return {};
    }
    return std::move(*mono);
}

std::optional<std::vector<float>> AudioRecording::copyMonoFrames(
    const std::uint64_t first_frame,
    const std::size_t frame_count) const {
    const auto available_frames = frameCount();
    if (!isValid() || first_frame > available_frames
        || frame_count > available_frames - first_frame) {
        return std::nullopt;
    }

    std::vector<float> mono(frame_count);
    const auto first = static_cast<std::size_t>(first_frame);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        float sum = 0.0F;
        for (std::uint16_t channel = 0; channel < format_.channel_count; ++channel) {
            sum += interleaved_samples_[(first + frame) * format_.channel_count + channel];
        }
        mono[frame] = sum / static_cast<float>(format_.channel_count);
    }
    return mono;
}

std::optional<AudioRecording> AudioRecording::convertedForPlayback(
    const std::uint32_t target_sample_rate_hz,
    const std::uint16_t target_channel_count) const {
    const AudioFormat target_format{target_sample_rate_hz, target_channel_count};
    if (!isValid() || !target_format.isValid()) {
        return std::nullopt;
    }
    if (format_.sample_rate_hz == target_sample_rate_hz
        && format_.channel_count == target_channel_count) {
        return AudioRecording{format_, interleaved_samples_};
    }

    const auto source_frames = frameCount();
    if (source_frames == 0) {
        return AudioRecording{target_format, {}};
    }

    const long double target_frames_exact = static_cast<long double>(source_frames)
        * target_sample_rate_hz / format_.sample_rate_hz;
    const auto rounded_target_frames = std::round(target_frames_exact);
    if (rounded_target_frames > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::nullopt;
    }
    const auto target_frames = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(rounded_target_frames));
    if (target_frames > std::numeric_limits<std::size_t>::max() / target_channel_count) {
        return std::nullopt;
    }

    std::vector<float> converted;
    converted.resize(static_cast<std::size_t>(target_frames) * target_channel_count);

    const auto interpolatedSample = [this, source_frames](
                                        const long double source_position,
                                        const std::uint16_t channel) {
        const auto first = static_cast<std::uint64_t>(std::floor(source_position));
        const auto second = std::min(first + 1, source_frames - 1);
        const auto fraction = static_cast<float>(source_position - first);
        const auto first_sample = interleaved_samples_[
            static_cast<std::size_t>(first) * format_.channel_count + channel];
        const auto second_sample = interleaved_samples_[
            static_cast<std::size_t>(second) * format_.channel_count + channel];
        return first_sample + (second_sample - first_sample) * fraction;
    };

    for (std::uint64_t target_frame = 0; target_frame < target_frames; ++target_frame) {
        const auto source_position = std::min(
            static_cast<long double>(target_frame) * format_.sample_rate_hz / target_sample_rate_hz,
            static_cast<long double>(source_frames - 1));

        if (format_.channel_count == target_channel_count) {
            for (std::uint16_t channel = 0; channel < target_channel_count; ++channel) {
                converted[static_cast<std::size_t>(target_frame) * target_channel_count + channel]
                    = interpolatedSample(source_position, channel);
            }
            continue;
        }

        float mono = 0.0F;
        for (std::uint16_t channel = 0; channel < format_.channel_count; ++channel) {
            mono += interpolatedSample(source_position, channel);
        }
        mono /= static_cast<float>(format_.channel_count);
        for (std::uint16_t channel = 0; channel < target_channel_count; ++channel) {
            converted[static_cast<std::size_t>(target_frame) * target_channel_count + channel] = mono;
        }
    }

    return AudioRecording{target_format, std::move(converted)};
}

RecordingController::RecordingController(
    IAudioCapture& capture,
    const double maximum_duration_seconds,
    const float clipping_threshold)
    : capture_(capture),
      maximum_duration_seconds_(maximum_duration_seconds),
      clipping_threshold_(clipping_threshold) {
    if (!std::isfinite(maximum_duration_seconds_) || maximum_duration_seconds_ <= 0.0) {
        throw std::invalid_argument("maximum recording duration must be positive");
    }
    if (!std::isfinite(clipping_threshold_) || clipping_threshold_ <= 0.0F
        || clipping_threshold_ > 1.0F) {
        throw std::invalid_argument("clipping threshold must be in (0, 1]");
    }
}

RecordingController::~RecordingController() {
    shutdown();
}

StartRecordingResult RecordingController::start(const std::string& device_id) {
    if (state_ == RecordingState::closed) {
        return StartRecordingResult::closed;
    }
    if (state_ == RecordingState::starting
        || state_ == RecordingState::recording
        || state_ == RecordingState::stopping
        || state_ == RecordingState::error) {
        return StartRecordingResult::busy;
    }

    const auto permission = capture_.permissionStatus();
    if (permission != PermissionStatus::granted) {
        last_error_ = permission == PermissionStatus::denied
            ? ServiceStatus::permission_denied
            : ServiceStatus::permission_required;
        state_ = RecordingState::error;
        return startResultFor(last_error_);
    }

    const auto format = capture_.preferredFormat(device_id);
    if (!format) {
        last_error_ = ServiceStatus::unavailable;
        state_ = RecordingState::error;
        return StartRecordingResult::device_unavailable;
    }
    if (!format->isValid()) {
        last_error_ = ServiceStatus::invalid_input;
        state_ = RecordingState::error;
        return StartRecordingResult::invalid_format;
    }

    state_ = RecordingState::starting;
    last_error_ = ServiceStatus::ok;
    last_stop_reason_ = RecordingStopReason::none;

    try {
        callback_state_ = std::make_shared<CallbackState>(
            std::make_shared<BoundedCaptureBuffer>(
                *format, maximum_duration_seconds_, clipping_threshold_));
    } catch (const std::exception&) {
        callback_state_.reset();
        last_error_ = ServiceStatus::invalid_input;
        state_ = RecordingState::error;
        return StartRecordingResult::invalid_format;
    }

    const std::weak_ptr<CallbackState> weak_state = callback_state_;
    const auto result = capture_.start(
        device_id,
        *format,
        [weak_state](const AudioBufferView audio) {
            if (const auto state = weak_state.lock()) {
                if (!state->buffer->append(audio)) {
                    state->failure.store(ServiceStatus::invalid_input, std::memory_order_release);
                }
            }
        },
        [weak_state](const ServiceStatus failure) {
            if (const auto state = weak_state.lock()) {
                state->failure.store(failure, std::memory_order_release);
            }
        });

    if (result != ServiceStatus::ok) {
        callback_state_.reset();
        last_error_ = result;
        state_ = RecordingState::error;
        return startResultFor(result);
    }

    state_ = RecordingState::recording;
    return StartRecordingResult::started;
}

bool RecordingController::stop(const RecordingStopReason reason) {
    if (state_ != RecordingState::recording && state_ != RecordingState::starting) {
        return false;
    }
    return finishActiveSession(reason, false);
}

std::optional<RecordingStopReason> RecordingController::service() {
    if (state_ != RecordingState::recording || !callback_state_) {
        return std::nullopt;
    }

    const auto failure = callback_state_->failure.load(std::memory_order_acquire);
    if (failure != ServiceStatus::ok) {
        last_error_ = failure;
        static_cast<void>(finishActiveSession(RecordingStopReason::device_failure, true));
        return RecordingStopReason::device_failure;
    }
    if (callback_state_->buffer->limitReached()) {
        static_cast<void>(finishActiveSession(RecordingStopReason::duration_limit, false));
        return RecordingStopReason::duration_limit;
    }
    return std::nullopt;
}

bool RecordingController::acknowledgeError() noexcept {
    if (state_ != RecordingState::error) {
        return false;
    }
    last_error_ = ServiceStatus::ok;
    state_ = recording_ ? RecordingState::ready : RecordingState::idle;
    return true;
}

bool RecordingController::clearRecording() noexcept {
    if (state_ == RecordingState::starting
        || state_ == RecordingState::recording
        || state_ == RecordingState::stopping
        || state_ == RecordingState::closed) {
        return false;
    }
    recording_.reset();
    last_stop_reason_ = RecordingStopReason::none;
    state_ = RecordingState::idle;
    return true;
}

void RecordingController::shutdown() noexcept {
    if (state_ == RecordingState::closed) {
        return;
    }
    if (state_ == RecordingState::starting || state_ == RecordingState::recording) {
        state_ = RecordingState::stopping;
        capture_.stop();
    }
    callback_state_.reset();
    recording_.reset();
    last_stop_reason_ = RecordingStopReason::shutdown;
    state_ = RecordingState::closed;
}

RecordingState RecordingController::state() const noexcept {
    return state_;
}

ServiceStatus RecordingController::lastError() const noexcept {
    return last_error_;
}

RecordingStopReason RecordingController::lastStopReason() const noexcept {
    return last_stop_reason_;
}

double RecordingController::maximumDurationSeconds() const noexcept {
    return maximum_duration_seconds_;
}

std::optional<AudioFormat> RecordingController::currentFormat() const noexcept {
    if (callback_state_) {
        return callback_state_->buffer->format();
    }
    if (recording_) {
        return recording_->format();
    }
    return std::nullopt;
}

RecordingMetrics RecordingController::pollMetrics() noexcept {
    if (callback_state_) {
        return callback_state_->buffer->pollMetrics();
    }
    if (recording_) {
        return {
            recording_->frameCount(),
            recording_->sampleCount(),
            recording_->durationSeconds(),
            0.0F,
            recording_->peakLevel(),
            recording_->peakLevel() >= clipping_threshold_,
            last_stop_reason_ == RecordingStopReason::duration_limit,
        };
    }
    return {};
}

std::shared_ptr<const AudioRecording> RecordingController::recording() const noexcept {
    return recording_;
}

std::optional<std::vector<float>> RecordingController::copyMonoFrames(
    const std::uint64_t first_frame,
    const std::size_t frame_count) const {
    if (callback_state_) {
        return callback_state_->buffer->copyMonoFrames(first_frame, frame_count);
    }
    if (recording_) {
        return recording_->copyMonoFrames(first_frame, frame_count);
    }
    return std::nullopt;
}

bool RecordingController::finishActiveSession(
    const RecordingStopReason reason,
    const bool failed) noexcept {
    if (!callback_state_) {
        return false;
    }

    state_ = RecordingState::stopping;
    capture_.stop();

    try {
        auto completed = std::make_shared<AudioRecording>(
            callback_state_->buffer->format(),
            callback_state_->buffer->snapshot());
        if (!failed || completed->sampleCount() > 0) {
            recording_ = std::move(completed);
        }
    } catch (const std::exception&) {
        last_error_ = ServiceStatus::io_error;
        callback_state_.reset();
        last_stop_reason_ = RecordingStopReason::device_failure;
        state_ = RecordingState::error;
        return false;
    }

    callback_state_.reset();
    last_stop_reason_ = reason;
    state_ = failed ? RecordingState::error : RecordingState::ready;
    return true;
}

} // namespace hum::core
