#pragma once
#include "hum/core/audio.hpp"
#include <memory>
#include <mutex>

namespace hum::app {
// Opens the device during preparation but discards all count-in audio. Switching
// to capture only installs callbacks, avoiding hardware startup on the downbeat.
class ArmedCapture final : public core::IAudioCapture {
    struct Session {
        std::mutex mutex;
        core::AudioBlockCallback audio;
        core::CaptureFailureCallback failure;
        core::ServiceStatus error{core::ServiceStatus::ok};
        bool open{true};
    };
public:
    explicit ArmedCapture(core::IAudioCapture& source) : source_(source) {}
    ~ArmedCapture() override { stop(); }
    auto devices() const -> std::vector<core::AudioInputDevice> override { return source_.devices(); }
    auto permissionStatus() const -> core::PermissionStatus override { return source_.permissionStatus(); }
    void requestPermission(core::PermissionCallback callback) override { source_.requestPermission(std::move(callback)); }
    auto preferredFormat(const std::string& id) const -> std::optional<core::AudioFormat> override { return source_.preferredFormat(id); }
    core::ServiceStatus arm(const std::string& id) {
        stop();
        if (permissionStatus() != core::PermissionStatus::granted) { return core::ServiceStatus::permission_denied; }
        const auto format = preferredFormat(id);
        if (!format || !format->isValid()) { return core::ServiceStatus::unavailable; }
        id_ = id; format_ = *format; session_ = std::make_shared<Session>();
        auto session = session_;
        const auto result = source_.start(id, *format, [session](core::AudioBufferView block) {
            std::lock_guard lock(session->mutex);
            if (session->open && session->audio && session->error == core::ServiceStatus::ok) { session->audio(block); }
        }, [session](core::ServiceStatus error) {
            std::lock_guard lock(session->mutex);
            if (!session->open) { return; }
            session->error = error;
            if (session->failure) { session->failure(error); }
        });
        if (result != core::ServiceStatus::ok) { stop(); }
        return result;
    }
    core::ServiceStatus start(const std::string& id, core::AudioFormat format,
        core::AudioBlockCallback audio, core::CaptureFailureCallback failure) override {
        if (!session_) { const auto status = arm(id); if (status != core::ServiceStatus::ok) { return status; } }
        std::lock_guard lock(session_->mutex);
        if (id != id_ || format.sample_rate_hz != format_.sample_rate_hz || format.channel_count != format_.channel_count) {
            return core::ServiceStatus::invalid_input;
        }
        if (session_->error != core::ServiceStatus::ok) { return session_->error; }
        if (session_->audio) { return core::ServiceStatus::busy; }
        session_->audio = std::move(audio); session_->failure = std::move(failure);
        return core::ServiceStatus::ok;
    }
    void stop() noexcept override {
        if (session_) {
            { std::lock_guard lock(session_->mutex); session_->open = false; session_->audio = {}; session_->failure = {}; }
            source_.stop(); session_.reset();
        }
    }
    bool isCapturing() const noexcept override { return source_.isCapturing(); }
    core::ServiceStatus error() const {
        if (!session_) { return core::ServiceStatus::ok; }
        std::lock_guard lock(session_->mutex); return session_->error;
    }
private:
    core::IAudioCapture& source_;
    std::shared_ptr<Session> session_;
    std::string id_;
    core::AudioFormat format_{};
};
}
