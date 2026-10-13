#include "QtAudioCapture.hpp"

#include <QAudioFormat>
#include <QAudioSource>
#include <QCoreApplication>
#include <QIODevice>
#include <QPermission>
#include <QPermissions>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace hum::app {

namespace {

[[nodiscard]] core::PermissionStatus toCorePermission(const Qt::PermissionStatus status) noexcept {
    switch (status) {
    case Qt::PermissionStatus::Granted:
        return core::PermissionStatus::granted;
    case Qt::PermissionStatus::Denied:
        return core::PermissionStatus::denied;
    case Qt::PermissionStatus::Undetermined:
        return core::PermissionStatus::unknown;
    }
    return core::PermissionStatus::unknown;
}

class CaptureIODevice final : public QIODevice {
public:
    CaptureIODevice(
        QAudioFormat format,
        core::AudioBlockCallback audio_callback,
        core::CaptureFailureCallback failure_callback,
        QObject* parent)
        : QIODevice(parent),
          format_(std::move(format)),
          audio_callback_(std::move(audio_callback)),
          failure_callback_(std::move(failure_callback)) {
    }

protected:
    qint64 readData(char*, qint64) override {
        return 0;
    }

    qint64 writeData(const char* data, const qint64 maximum_size) override {
        const auto bytes_per_sample = format_.bytesPerSample();
        if (data == nullptr || maximum_size < 0 || bytes_per_sample <= 0
            || maximum_size % bytes_per_sample != 0) {
            if (failure_callback_) {
                failure_callback_(core::ServiceStatus::invalid_input);
            }
            return maximum_size;
        }

        const auto total_samples = maximum_size / bytes_per_sample;
        qint64 samples_read = 0;
        while (samples_read < total_samples) {
            const auto chunk_size = static_cast<std::size_t>(std::min<qint64>(
                total_samples - samples_read,
                static_cast<qint64>(scratch_.size())));
            const auto* chunk = data + samples_read * bytes_per_sample;

            for (std::size_t index = 0; index < chunk_size; ++index) {
                const auto* sample = chunk + static_cast<qint64>(index) * bytes_per_sample;
                scratch_[index] = normalizedSample(sample);
            }

            if (audio_callback_) {
                audio_callback_({
                    std::span<const float>{scratch_.data(), chunk_size},
                    static_cast<std::uint32_t>(format_.sampleRate()),
                    static_cast<std::uint16_t>(format_.channelCount()),
                });
            }
            samples_read += static_cast<qint64>(chunk_size);
        }
        return maximum_size;
    }

private:
    [[nodiscard]] float normalizedSample(const char* sample) const noexcept {
        switch (format_.sampleFormat()) {
        case QAudioFormat::UInt8: {
            std::uint8_t value{};
            std::memcpy(&value, sample, sizeof(value));
            return (static_cast<float>(value) - 128.0F) / 128.0F;
        }
        case QAudioFormat::Int16: {
            std::int16_t value{};
            std::memcpy(&value, sample, sizeof(value));
            return static_cast<float>(value) / 32'768.0F;
        }
        case QAudioFormat::Int32: {
            std::int32_t value{};
            std::memcpy(&value, sample, sizeof(value));
            return static_cast<float>(static_cast<double>(value) / 2'147'483'648.0);
        }
        case QAudioFormat::Float: {
            float value{};
            std::memcpy(&value, sample, sizeof(value));
            return value;
        }
        case QAudioFormat::Unknown:
        case QAudioFormat::NSampleFormats:
            return 0.0F;
        }
        return 0.0F;
    }

    QAudioFormat format_;
    core::AudioBlockCallback audio_callback_;
    core::CaptureFailureCallback failure_callback_;
    std::array<float, 4'096> scratch_{};
};

} // namespace

QtAudioCapture::QtAudioCapture(QObject* parent)
    : QObject(parent),
      media_devices_(this) {
    connect(&media_devices_, &QMediaDevices::audioInputsChanged, this, [this] {
        handleDevicesChanged();
    });
}

QtAudioCapture::~QtAudioCapture() {
    stop();
}

std::vector<core::AudioInputDevice> QtAudioCapture::devices() const {
    std::vector<core::AudioInputDevice> result;
    const auto inputs = QMediaDevices::audioInputs();
    result.reserve(static_cast<std::size_t>(inputs.size()));
    for (const auto& device : inputs) {
        result.push_back({stableId(device), device.description().toStdString(), device.isDefault()});
    }
    return result;
}

core::PermissionStatus QtAudioCapture::permissionStatus() const {
#if QT_CONFIG(permissions)
    if (auto* application = QCoreApplication::instance()) {
        return toCorePermission(application->checkPermission(QMicrophonePermission{}));
    }
    return core::PermissionStatus::unknown;
#else
    return core::PermissionStatus::granted;
#endif
}

void QtAudioCapture::requestPermission(core::PermissionCallback callback) {
#if QT_CONFIG(permissions)
    if (auto* application = QCoreApplication::instance()) {
        application->requestPermission(
            QMicrophonePermission{}, this,
            [callback = std::move(callback)](const QPermission& permission) {
                if (callback) {
                    callback(toCorePermission(permission.status()));
                }
            });
        return;
    }
#endif
    if (callback) {
        callback(core::PermissionStatus::granted);
    }
}

std::optional<core::AudioFormat> QtAudioCapture::preferredFormat(
    const std::string& device_id) const {
    const auto device = findDevice(device_id);
    if (!device) {
        return std::nullopt;
    }
    const auto format = device->preferredFormat();
    if (!format.isValid()
        || format.channelCount() > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
        return std::nullopt;
    }
    return core::AudioFormat{
        static_cast<std::uint32_t>(format.sampleRate()),
        static_cast<std::uint16_t>(format.channelCount()),
    };
}

core::ServiceStatus QtAudioCapture::start(
    const std::string& device_id,
    const core::AudioFormat format,
    core::AudioBlockCallback audio_callback,
    core::CaptureFailureCallback failure_callback) {
    if (isCapturing()) {
        return core::ServiceStatus::busy;
    }
    if (permissionStatus() != core::PermissionStatus::granted) {
        return permissionStatus() == core::PermissionStatus::denied
            ? core::ServiceStatus::permission_denied
            : core::ServiceStatus::permission_required;
    }

    const auto device = findDevice(device_id);
    if (!device) {
        return core::ServiceStatus::unavailable;
    }
    auto qt_format = device->preferredFormat();
    qt_format.setSampleRate(static_cast<int>(format.sample_rate_hz));
    qt_format.setChannelCount(static_cast<int>(format.channel_count));
    if (!qt_format.isValid() || !device->isFormatSupported(qt_format)) {
        return core::ServiceStatus::invalid_input;
    }

    stopping_ = false;
    active_device_id_ = device_id;
    failure_callback_ = std::move(failure_callback);
    capture_device_ = std::make_unique<CaptureIODevice>(
        qt_format, std::move(audio_callback), failure_callback_, this);
    if (!capture_device_->open(QIODevice::WriteOnly)) {
        capture_device_.reset();
        failure_callback_ = {};
        active_device_id_.clear();
        return core::ServiceStatus::io_error;
    }

    audio_source_ = std::make_unique<QAudioSource>(*device, qt_format, this);
    if (audio_source_->isNull()) {
        stop();
        return core::ServiceStatus::unavailable;
    }
    connect(audio_source_.get(), &QAudioSource::stateChanged, this, [this](const QAudio::State state) {
        if (state == QAudio::StoppedState && !stopping_ && audio_source_
            && audio_source_->error() != QAudio::NoError && failure_callback_) {
            failure_callback_(core::ServiceStatus::io_error);
        }
    });
    audio_source_->start(capture_device_.get());
    if (audio_source_->error() != QAudio::NoError) {
        stop();
        return core::ServiceStatus::io_error;
    }
    return core::ServiceStatus::ok;
}

void QtAudioCapture::stop() noexcept {
    stopping_ = true;
    if (audio_source_) {
        audio_source_->stop();
    }
    if (capture_device_) {
        capture_device_->close();
    }
    audio_source_.reset();
    capture_device_.reset();
    failure_callback_ = {};
    active_device_id_.clear();
    stopping_ = false;
}

bool QtAudioCapture::isCapturing() const noexcept {
    return audio_source_ != nullptr;
}

void QtAudioCapture::setDevicesChangedCallback(std::function<void()> callback) {
    devices_changed_callback_ = std::move(callback);
}

std::optional<QAudioDevice> QtAudioCapture::findDevice(const std::string& device_id) const {
    const auto inputs = QMediaDevices::audioInputs();
    const auto found = std::find_if(inputs.cbegin(), inputs.cend(), [&device_id](const QAudioDevice& device) {
        return stableId(device) == device_id;
    });
    if (found == inputs.cend()) {
        return std::nullopt;
    }
    return *found;
}

std::string QtAudioCapture::stableId(const QAudioDevice& device) {
    return device.id().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString();
}

void QtAudioCapture::handleDevicesChanged() {
    if (!active_device_id_.empty() && !findDevice(active_device_id_) && failure_callback_) {
        failure_callback_(core::ServiceStatus::device_disconnected);
    }
    if (devices_changed_callback_) {
        devices_changed_callback_();
    }
}

} // namespace hum::app
