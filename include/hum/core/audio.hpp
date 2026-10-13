#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hum::core {

struct AudioFormat {
    std::uint32_t sample_rate_hz{0};
    std::uint16_t channel_count{0};

    [[nodiscard]] bool isValid() const noexcept {
        return sample_rate_hz > 0 && channel_count > 0 && channel_count <= 32;
    }
};

struct AudioBufferView {
    std::span<const float> interleaved_samples;
    std::uint32_t sample_rate_hz{0};
    std::uint16_t channel_count{0};

    [[nodiscard]] bool isValid() const noexcept {
        return sample_rate_hz > 0 && channel_count > 0
            && interleaved_samples.size() % channel_count == 0;
    }
};

enum class ServiceStatus {
    ok,
    busy,
    unavailable,
    permission_required,
    permission_denied,
    device_disconnected,
    invalid_input,
    io_error,
};

enum class PermissionStatus {
    unknown,
    granted,
    denied,
};

struct AudioInputDevice {
    std::string id;
    std::string name;
    bool is_default{false};
};

using AudioBlockCallback = std::function<void(AudioBufferView)>;
using CaptureFailureCallback = std::function<void(ServiceStatus)>;
using PermissionCallback = std::function<void(PermissionStatus)>;

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    [[nodiscard]] virtual std::vector<AudioInputDevice> devices() const = 0;
    [[nodiscard]] virtual PermissionStatus permissionStatus() const = 0;
    virtual void requestPermission(PermissionCallback callback) = 0;
    [[nodiscard]] virtual std::optional<AudioFormat> preferredFormat(
        const std::string& device_id) const = 0;
    virtual ServiceStatus start(
        const std::string& device_id,
        AudioFormat format,
        AudioBlockCallback audio_callback,
        CaptureFailureCallback failure_callback) = 0;
    virtual void stop() noexcept = 0;
    [[nodiscard]] virtual bool isCapturing() const noexcept = 0;
};

} // namespace hum::core
