#pragma once

#include "hum/core/audio.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hum::test {

class FakeAudioCapture final : public core::IAudioCapture {
public:
    std::vector<core::AudioInputDevice> available_devices{{"fake-1", "Test microphone", true}};
    core::PermissionStatus permission{core::PermissionStatus::granted};
    core::AudioFormat device_format{48'000, 1};
    core::ServiceStatus start_result{core::ServiceStatus::ok};
    int start_count{0};
    int stop_count{0};

    [[nodiscard]] std::vector<core::AudioInputDevice> devices() const override {
        return available_devices;
    }

    [[nodiscard]] core::PermissionStatus permissionStatus() const override {
        return permission;
    }

    void requestPermission(core::PermissionCallback callback) override {
        if (callback) {
            callback(permission);
        }
    }

    [[nodiscard]] std::optional<core::AudioFormat> preferredFormat(
        const std::string& device_id) const override {
        const auto found = std::find_if(
            available_devices.cbegin(), available_devices.cend(),
            [&device_id](const core::AudioInputDevice& device) { return device.id == device_id; });
        if (found == available_devices.cend()) {
            return std::nullopt;
        }
        return device_format;
    }

    core::ServiceStatus start(
        const std::string& device_id,
        const core::AudioFormat format,
        core::AudioBlockCallback audio_callback,
        core::CaptureFailureCallback failure_callback) override {
        ++start_count;
        if (!preferredFormat(device_id) || start_result != core::ServiceStatus::ok) {
            return preferredFormat(device_id) ? start_result : core::ServiceStatus::unavailable;
        }
        active_ = true;
        active_format_ = format;
        audio_callback_ = std::move(audio_callback);
        failure_callback_ = std::move(failure_callback);
        return core::ServiceStatus::ok;
    }

    void stop() noexcept override {
        if (active_) {
            ++stop_count;
        }
        active_ = false;
        audio_callback_ = {};
        failure_callback_ = {};
    }

    [[nodiscard]] bool isCapturing() const noexcept override {
        return active_;
    }

    void emit(const std::vector<float>& samples) {
        if (active_ && audio_callback_) {
            audio_callback_({samples, active_format_.sample_rate_hz, active_format_.channel_count});
        }
    }

    void fail(const core::ServiceStatus status) {
        if (active_ && failure_callback_) {
            failure_callback_(status);
        }
    }

private:
    bool active_{false};
    core::AudioFormat active_format_{};
    core::AudioBlockCallback audio_callback_;
    core::CaptureFailureCallback failure_callback_;
};

} // namespace hum::test
