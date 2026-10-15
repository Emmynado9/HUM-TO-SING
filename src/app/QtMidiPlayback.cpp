#include "QtMidiPlayback.hpp"

#include "hum/core/simple_synth.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <QFutureWatcher>
#include <QMediaDevices>
#include <QMetaObject>
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

struct PreparedMelody {
    QAudioFormat format;
    QByteArray bytes;
    double phrase_duration_seconds{0.0};
    QString error;
};

[[nodiscard]] PreparedMelody prepareMelody(
    const std::vector<core::TimedMidiNote>& notes,
    const QAudioFormat& target_format) {
    core::SimpleSynthConfig config;
    config.sample_rate_hz = static_cast<std::uint32_t>(target_format.sampleRate());
    config.channel_count = static_cast<std::uint16_t>(target_format.channelCount());
    const auto rendered = core::SimpleSynthRenderer{}.render(notes, config);
    if (!rendered) {
        return {{}, {}, 0.0,
            QStringLiteral("The melody could not be rendered safely. Check its notes and duration.")};
    }

    const auto bytes_per_sample = target_format.bytesPerSample();
    if (bytes_per_sample <= 0
        || rendered->interleaved_samples.size()
            > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max() / bytes_per_sample)) {
        return {{}, {}, 0.0, QStringLiteral("The melody playback buffer is too large.")};
    }
    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(rendered->interleaved_samples.size()) * bytes_per_sample);
    auto* output = bytes.data();
    for (std::size_t index = 0; index < rendered->interleaved_samples.size(); ++index) {
        const auto sample = std::clamp(rendered->interleaved_samples[index], -1.0F, 1.0F);
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
            return {{}, {}, 0.0,
                QStringLiteral("The output device reported an unsupported sample format.")};
        }
    }
    return {target_format, std::move(bytes), rendered->phrase_duration_seconds, {}};
}

} // namespace

class QtMidiPlayback::Impl final : public std::enable_shared_from_this<QtMidiPlayback::Impl> {
public:
    explicit Impl(QtMidiPlayback* owner)
        : owner_(owner), media_devices_(std::make_unique<QMediaDevices>()) {
        QObject::connect(media_devices_.get(), &QMediaDevices::audioOutputsChanged, owner_, [this] {
            if (!playing_ && !preparing_) {
                return;
            }
            stop();
            report(QStringLiteral(
                "Melody playback stopped because the audio output changed. Press Play melody to restart."), true);
        });
    }

    void play(std::vector<core::TimedMidiNote> notes, QtMidiPlayback::StatusCallback callback) {
        stop();
        status_callback_ = std::move(callback);
        output_device_ = QMediaDevices::defaultAudioOutput();
        if (output_device_.isNull()) {
            report(QStringLiteral(
                "No audio output is available. Connect speakers or headphones and try again."), true);
            return;
        }
        const auto target_format = output_device_.preferredFormat();
        if (!target_format.isValid() || !output_device_.isFormatSupported(target_format)) {
            report(QStringLiteral("The selected audio output has no supported playback format."), true);
            return;
        }

        preparing_ = true;
        const auto generation = generation_;
        report(QStringLiteral("Preparing the built-in instrument…"), false);
        auto* watcher = new QFutureWatcher<PreparedMelody>(owner_);
        const auto weak_impl = weak_from_this();
        QObject::connect(watcher, &QFutureWatcher<PreparedMelody>::finished, owner_,
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
            [notes = std::move(notes), target_format] {
                return prepareMelody(notes, target_format);
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
        position_at_stop_seconds_ = 0.0;
        phrase_duration_seconds_ = 0.0;
        manual_stop_ = false;
    }

    [[nodiscard]] bool isPlaying() const noexcept { return playing_; }
    [[nodiscard]] bool isPreparing() const noexcept { return preparing_; }

    [[nodiscard]] double positionSeconds() const noexcept {
        if (playing_ && sink_) {
            return std::clamp(
                static_cast<double>(sink_->processedUSecs()) / 1'000'000.0,
                0.0,
                phrase_duration_seconds_);
        }
        return position_at_stop_seconds_;
    }

    [[nodiscard]] double phraseDurationSeconds() const noexcept {
        return phrase_duration_seconds_;
    }

private:
    void startPrepared(PreparedMelody prepared) {
        if (!output_device_.isFormatSupported(prepared.format)) {
            report(QStringLiteral("The audio output changed while the melody was being prepared."), true);
            return;
        }
        bytes_ = std::move(prepared.bytes);
        phrase_duration_seconds_ = prepared.phrase_duration_seconds;
        position_at_stop_seconds_ = 0.0;
        buffer_ = std::make_unique<QBuffer>(&bytes_, owner_);
        if (!buffer_->open(QIODevice::ReadOnly)) {
            report(QStringLiteral("The in-memory melody buffer could not be opened."), true);
            return;
        }
        sink_ = std::make_unique<QAudioSink>(output_device_, prepared.format, owner_);
        if (sink_->isNull()) {
            sink_.reset();
            buffer_.reset();
            bytes_.clear();
            report(QStringLiteral("The audio output could not be opened."), true);
            return;
        }
        QObject::connect(sink_.get(), &QAudioSink::stateChanged, owner_, [this](const QAudio::State state) {
            if (manual_stop_ || !sink_) {
                return;
            }
            if (state == QAudio::IdleState && sink_->error() == QAudio::NoError) {
                playing_ = false;
                position_at_stop_seconds_ = phrase_duration_seconds_;
                report(QStringLiteral("Melody playback complete."), false);
            } else if (state == QAudio::StoppedState && sink_->error() != QAudio::NoError) {
                playing_ = false;
                position_at_stop_seconds_ = 0.0;
                report(QStringLiteral(
                    "Melody playback stopped because the audio output failed. Press Play melody to retry."), true);
            }
        });
        playing_ = true;
        sink_->start(buffer_.get());
        if (sink_->error() != QAudio::NoError) {
            playing_ = false;
            position_at_stop_seconds_ = 0.0;
            report(QStringLiteral("Melody playback could not start on the selected output."), true);
            return;
        }
        report(QStringLiteral("Playing the visible melody with the built-in soft-tone instrument…"), false);
    }

    void report(const QString& message, const bool error) {
        if (status_callback_) {
            status_callback_(message, error);
        }
    }

    QtMidiPlayback* owner_;
    std::unique_ptr<QMediaDevices> media_devices_;
    QAudioDevice output_device_;
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<QBuffer> buffer_;
    QByteArray bytes_;
    QtMidiPlayback::StatusCallback status_callback_;
    std::uint64_t generation_{0};
    double phrase_duration_seconds_{0.0};
    double position_at_stop_seconds_{0.0};
    bool playing_{false};
    bool preparing_{false};
    bool manual_stop_{false};
};

QtMidiPlayback::QtMidiPlayback(QObject* parent)
    : QObject(parent), impl_(std::make_shared<Impl>(this)) {
}

QtMidiPlayback::~QtMidiPlayback() {
    impl_->stop();
    impl_.reset();
}

void QtMidiPlayback::play(
    std::vector<core::TimedMidiNote> notes,
    StatusCallback callback) {
    impl_->play(std::move(notes), std::move(callback));
}

void QtMidiPlayback::stop() noexcept { impl_->stop(); }
bool QtMidiPlayback::isPlaying() const noexcept { return impl_->isPlaying(); }
bool QtMidiPlayback::isPreparing() const noexcept { return impl_->isPreparing(); }
double QtMidiPlayback::positionSeconds() const noexcept { return impl_->positionSeconds(); }
double QtMidiPlayback::phraseDurationSeconds() const noexcept {
    return impl_->phraseDurationSeconds();
}

} // namespace hum::app
