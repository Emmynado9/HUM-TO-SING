#pragma once

#include "hum/core/audio.hpp"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QAudioSource;
class QIODevice;

namespace hum::app {

class MacAudioInput;

class QtAudioCapture final : public QObject, public core::IAudioCapture {
public:
    explicit QtAudioCapture(QObject* parent = nullptr);
    ~QtAudioCapture() override;

    [[nodiscard]] std::vector<core::AudioInputDevice> devices() const override;
    [[nodiscard]] core::PermissionStatus permissionStatus() const override;
    void requestPermission(core::PermissionCallback callback) override;
    [[nodiscard]] std::optional<core::AudioFormat> preferredFormat(
        const std::string& device_id) const override;
    core::ServiceStatus start(
        const std::string& device_id,
        core::AudioFormat format,
        core::AudioBlockCallback audio_callback,
        core::CaptureFailureCallback failure_callback) override;
    void stop() noexcept override;
    [[nodiscard]] bool isCapturing() const noexcept override;

    void setDevicesChangedCallback(std::function<void()> callback);

private:
    [[nodiscard]] std::optional<QAudioDevice> findDevice(const std::string& device_id) const;
    [[nodiscard]] static std::string stableId(const QAudioDevice& device);
    void handleDevicesChanged();

    QMediaDevices media_devices_;
    std::unique_ptr<QAudioSource> audio_source_;
    std::unique_ptr<QIODevice> capture_device_;
    core::CaptureFailureCallback failure_callback_;
    std::function<void()> devices_changed_callback_;
    std::string active_device_id_;
    bool stopping_{false};
#ifdef Q_OS_MACOS
    std::unique_ptr<MacAudioInput> native_input_;
#endif
};

} // namespace hum::app
