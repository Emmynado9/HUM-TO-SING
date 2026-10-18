#include "IsolatedMicRecorder.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace hum::app::vocal {
struct IsolatedMicRecorder::Session {
    mutable std::mutex mutex;
    hum::vocal::AudioBuffer buffer;
    std::size_t capacity{0};
    std::uint16_t channels{0};
    float peak{0};
    bool accepting{true}, clipped{false}, full{false};
    core::ServiceStatus error{core::ServiceStatus::ok};
};
IsolatedMicRecorder::IsolatedMicRecorder(core::IAudioCapture& capture, double limit_seconds)
    : capture_(capture), limit_(std::isfinite(limit_seconds) ? std::clamp(limit_seconds, 0.1, 60.0) : 30.0) {}
IsolatedMicRecorder::~IsolatedMicRecorder() { discard(); }

core::ServiceStatus IsolatedMicRecorder::start(const std::string& device) {
    if (recording_) { return core::ServiceStatus::busy; }
    if (capture_.permissionStatus() != core::PermissionStatus::granted) {
        return capture_.permissionStatus() == core::PermissionStatus::denied
            ? core::ServiceStatus::permission_denied : core::ServiceStatus::permission_required;
    }
    const auto format = capture_.preferredFormat(device);
    if (!format) { return core::ServiceStatus::unavailable; }
    if (!format->isValid() || format->sample_rate_hz < 8'000 || format->sample_rate_hz > 192'000) {
        return core::ServiceStatus::invalid_input;
    }
    auto session = std::make_shared<Session>();
    session->buffer.sample_rate = format->sample_rate_hz;
    session->channels = format->channel_count;
    session->capacity = static_cast<std::size_t>(limit_ * format->sample_rate_hz);
    session->buffer.samples.reserve(session->capacity);
    const auto status = capture_.start(device, *format, [session](core::AudioBufferView block) {
        std::lock_guard lock(session->mutex);
        if (!session->accepting) { return; }
        if (!block.isValid() || block.sample_rate_hz != session->buffer.sample_rate
            || block.channel_count != session->channels) {
            session->error = core::ServiceStatus::invalid_input; session->accepting = false; return;
        }
        session->peak = 0;
        const auto frames = std::min(block.interleaved_samples.size() / block.channel_count,
            session->capacity - session->buffer.samples.size());
        for (std::size_t frame = 0; frame < frames; ++frame) {
            double mono = 0;
            for (std::uint16_t channel = 0; channel < block.channel_count; ++channel) {
                const auto sample = block.interleaved_samples[frame * block.channel_count + channel];
                if (!std::isfinite(sample)) {
                    session->error = core::ServiceStatus::invalid_input; session->accepting = false; return;
                }
                session->peak = std::max(session->peak, std::abs(sample));
                session->clipped = session->clipped || std::abs(sample) >= .98F;
                mono += std::clamp(sample, -1.0F, 1.0F);
            }
            session->buffer.samples.push_back(static_cast<float>(mono / block.channel_count));
        }
        session->full = session->buffer.samples.size() >= session->capacity;
        if (session->full) { session->accepting = false; }
    }, [session](core::ServiceStatus failure) {
        std::lock_guard lock(session->mutex);
        session->error = failure; session->accepting = false;
    });
    if (status != core::ServiceStatus::ok) { capture_.stop(); return status; }
    session_ = std::move(session); audio_.reset(); recording_ = true;
    return status;
}
void IsolatedMicRecorder::stop() {
    if (!recording_) { return; }
    { std::lock_guard lock(session_->mutex); session_->accepting = false; }
    capture_.stop(); recording_ = false;
    std::lock_guard lock(session_->mutex);
    if (session_->error == core::ServiceStatus::ok && !session_->buffer.samples.empty()) {
        audio_ = std::make_shared<hum::vocal::AudioBuffer>(std::move(session_->buffer));
    } else { audio_.reset(); }
}
void IsolatedMicRecorder::discard() {
    stop(); audio_.reset(); session_.reset();
}
void IsolatedMicRecorder::poll() {
    if (recording_ && (limitReached() || error() != core::ServiceStatus::ok)) { stop(); }
}
bool IsolatedMicRecorder::limitReached() const {
    if (!session_) { return false; }
    std::lock_guard lock(session_->mutex); return session_->full;
}
core::ServiceStatus IsolatedMicRecorder::error() const {
    if (!session_) { return core::ServiceStatus::ok; }
    std::lock_guard lock(session_->mutex); return session_->error;
}
double IsolatedMicRecorder::elapsed() const {
    if (audio_) { return audio_->duration(); }
    if (!session_) { return 0; }
    std::lock_guard lock(session_->mutex); return session_->buffer.duration();
}
float IsolatedMicRecorder::peak() const {
    if (!session_ || !recording_) { return 0; }
    std::lock_guard lock(session_->mutex); return session_->peak;
}
bool IsolatedMicRecorder::clipped() const {
    if (!session_) { return false; }
    std::lock_guard lock(session_->mutex); return session_->clipped;
}
}
