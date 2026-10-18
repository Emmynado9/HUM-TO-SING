#include "QtAudioPlayback.hpp"
#include "AudioOutputFormat.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <QFutureWatcher>
#include <QMediaDevices>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace hum::app {

namespace {

struct PreparedAudio {
    QAudioFormat format;
    QByteArray bytes;
    QString error;
};

[[nodiscard]] PreparedAudio prepareAudio(
    const std::shared_ptr<const core::AudioRecording>& source,
    const QAudioFormat& target_format) {
    if (!source || !source->isValid() || source->sampleCount() == 0) {
        return {{}, {}, QStringLiteral("There is no recording to play.")};
    }

    const auto converted = source->convertedForPlayback(
        static_cast<std::uint32_t>(target_format.sampleRate()),
        static_cast<std::uint16_t>(target_format.channelCount()));
    if (!converted) {
        return {{}, {}, QStringLiteral("The recording could not be prepared for this output device.")};
    }

    const auto& samples = converted->interleavedSamples();
    const auto bytes_per_sample = target_format.bytesPerSample();
    if (bytes_per_sample <= 0
        || samples.size() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max() / bytes_per_sample)) {
        return {{}, {}, QStringLiteral("The playback buffer is too large.")};
    }

    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(samples.size()) * bytes_per_sample);
    auto* output = bytes.data();

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto sample = std::clamp(samples[index], -1.0F, 1.0F);
        auto* destination = output + static_cast<qsizetype>(index) * bytes_per_sample;
        switch (target_format.sampleFormat()) {
        case QAudioFormat::UInt8: {
            const auto value = static_cast<std::uint8_t>(std::lround((sample + 1.0F) * 127.5F));
            std::memcpy(destination, &value, sizeof(value));
            break;
        }
        case QAudioFormat::Int16: {
            const auto value = static_cast<std::int16_t>(std::lround(sample * 32'767.0F));
            std::memcpy(destination, &value, sizeof(value));
            break;
        }
        case QAudioFormat::Int32: {
            const auto value = static_cast<std::int32_t>(
                std::llround(static_cast<double>(sample) * 2'147'483'647.0));
            std::memcpy(destination, &value, sizeof(value));
            break;
        }
        case QAudioFormat::Float:
            std::memcpy(destination, &sample, sizeof(sample));
            break;
        case QAudioFormat::Unknown:
        case QAudioFormat::NSampleFormats:
            return {{}, {}, QStringLiteral("The output device reported an unsupported sample format.")};
        }
    }
    return {target_format, std::move(bytes), {}};
}

} // namespace

class QtAudioPlayback::Impl final : public std::enable_shared_from_this<QtAudioPlayback::Impl> {
public:
    explicit Impl(QtAudioPlayback* owner)
        : owner_(owner) {
    }

    void play(
        std::shared_ptr<const core::AudioRecording> recording,
        QtAudioPlayback::StatusCallback callback) {
        stop();
        status_callback_ = std::move(callback);

        output_device_ = QMediaDevices::defaultAudioOutput();
        if (output_device_.isNull()) {
            report(QStringLiteral("No audio output is available. Connect speakers or headphones and try again."), true);
            return;
        }
        const auto selected_format = chooseOutputFormat(output_device_);
        if (!selected_format) {
            report(QStringLiteral("The selected audio output has no supported playback format."), true);
            return;
        }

        const auto target_format = *selected_format;
        preparing_ = true;
        const auto generation = generation_;
        report(QStringLiteral("Preparing the original recording…"), false);

        auto* watcher = new QFutureWatcher<PreparedAudio>(owner_);
        const auto weak_impl = weak_from_this();
        QObject::connect(watcher, &QFutureWatcher<PreparedAudio>::finished, owner_,
            [weak_impl, watcher, generation] {
                auto prepared = watcher->result();
                watcher->deleteLater();
                const auto impl = weak_impl.lock();
                if (!impl || generation != impl->generation_) {
                    return;
                }
                impl->preparing_ = false;
                if (!prepared.error.isEmpty()) {
                    impl->report(prepared.error, true);
                    return;
                }
                impl->startPrepared(std::move(prepared));
            });
        watcher->setFuture(QtConcurrent::run(
            [recording = std::move(recording), target_format] {
                return prepareAudio(recording, target_format);
            }));
    }

    void stop() noexcept {
        ++generation_;
        preparing_ = false;
        manual_stop_ = true;
        if (sink_) {
            sink_->stop();
        }
        sink_.reset();
        buffer_.reset();
        bytes_.clear();
        playing_ = false;
        manual_stop_ = false;
    }

    [[nodiscard]] bool isPlaying() const noexcept {
        return playing_;
    }

    [[nodiscard]] bool isPreparing() const noexcept {
        return preparing_;
    }

private:
    void startPrepared(PreparedAudio prepared) {
        if (!output_device_.isFormatSupported(prepared.format)) {
            report(QStringLiteral("The audio output changed while playback was being prepared."), true);
            return;
        }

        bytes_ = std::move(prepared.bytes);
        buffer_ = std::make_unique<QBuffer>(&bytes_, owner_);
        if (!buffer_->open(QIODevice::ReadOnly)) {
            report(QStringLiteral("The in-memory playback buffer could not be opened."), true);
            return;
        }

        sink_ = std::make_unique<QAudioSink>(output_device_, prepared.format, owner_);
        if (sink_->isNull()) {
            report(QStringLiteral("The audio output could not be opened."), true);
            sink_.reset();
            buffer_.reset();
            bytes_.clear();
            return;
        }
        QObject::connect(sink_.get(), &QAudioSink::stateChanged, owner_, [this](const QAudio::State state) {
            if (manual_stop_ || !sink_) {
                return;
            }
            if (state == QAudio::IdleState && sink_->error() == QAudio::NoError) {
                playing_ = false;
                report(QStringLiteral("Playback complete. Your recording remains in memory."), false);
            } else if (state == QAudio::StoppedState && sink_->error() != QAudio::NoError) {
                playing_ = false;
                report(QStringLiteral("Playback stopped because the audio output failed."), true);
            }
        });

        playing_ = true;
        sink_->start(buffer_.get());
        if (sink_->error() != QAudio::NoError) {
            playing_ = false;
            report(QStringLiteral("Playback could not start on the selected output."), true);
            return;
        }
        report(QStringLiteral("Playing the original recording…"), false);
    }

    void report(const QString& message, const bool is_error) const {
        if (status_callback_) {
            status_callback_(message, is_error);
        }
    }

    QtAudioPlayback* owner_;
    std::uint64_t generation_{0};
    bool preparing_{false};
    bool playing_{false};
    bool manual_stop_{false};
    QAudioDevice output_device_;
    QByteArray bytes_;
    std::unique_ptr<QBuffer> buffer_;
    std::unique_ptr<QAudioSink> sink_;
    QtAudioPlayback::StatusCallback status_callback_;
};

QtAudioPlayback::QtAudioPlayback(QObject* parent)
    : QObject(parent),
      impl_(std::make_shared<Impl>(this)) {
}

QtAudioPlayback::~QtAudioPlayback() {
    stop();
}

void QtAudioPlayback::play(
    std::shared_ptr<const core::AudioRecording> recording,
    StatusCallback callback) {
    impl_->play(std::move(recording), std::move(callback));
}

void QtAudioPlayback::stop() noexcept {
    impl_->stop();
}

bool QtAudioPlayback::isPlaying() const noexcept {
    return impl_->isPlaying();
}

bool QtAudioPlayback::isPreparing() const noexcept {
    return impl_->isPreparing();
}

} // namespace hum::app
