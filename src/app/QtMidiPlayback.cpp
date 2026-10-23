#include "QtMidiPlayback.hpp"
#include "AudioOutputFormat.hpp"
#include "PlaybackBuffer.hpp"

#include "hum/core/simple_synth.hpp"
#ifdef __APPLE__
#include "MacInstrumentRenderer.hpp"
#endif

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
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
    const QAudioFormat& target_format, double tempo_bpm, bool metronome,
    std::vector<core::InstrumentVoice> voices, const std::optional<core::LoopRange> loop,
    const std::optional<core::OverdubPlan>& guide, const std::vector<core::VoiceClip>& voice_clips) {
    core::SimpleSynthConfig config;
    config.sample_rate_hz = static_cast<std::uint32_t>(target_format.sampleRate());
    config.channel_count = static_cast<std::uint16_t>(target_format.channelCount());
    config.tempo_bpm = tempo_bpm;
    config.metronome_enabled = metronome && voice_clips.empty();
    config.note_voices = std::move(voices);
    config.release_seconds = 1.2;
    if (notes.empty()) { config.release_seconds = 0.0; }
    std::optional<core::SynthRenderResult> rendered;
    if (notes.empty() && (guide || !voice_clips.empty())) {
        rendered = core::SynthRenderResult{{config.sample_rate_hz, config.channel_count}, {}, 0};
    } else {
#ifdef __APPLE__
    std::string instrument_error;
    rendered = audio::renderMacInstruments(notes, config, instrument_error);
    if (!rendered) { return {{}, {}, 0.0, QString::fromStdString(instrument_error)}; }
#else
    rendered = core::SimpleSynthRenderer{}.render(notes, config);
#endif
    }
    if (rendered && !core::mixVoiceClips(*rendered, guide ? guide->voice_clips : voice_clips,
        guide ? guide->origin : 0, guide ? guide->duration : 600)) {
        return {{}, {}, 0, QStringLiteral("Voice audio exceeds the playback memory limit.")};
    }
    if (!guide && metronome && rendered && !voice_clips.empty()) {
        for (std::size_t i = 0; i < rendered->interleaved_samples.size(); ++i)
            rendered->interleaved_samples[i] += core::metronomeSample(double(i / config.channel_count) / config.sample_rate_hz, tempo_bpm);
    }
    if (guide && rendered) { rendered = core::recordingGuide(*rendered, *guide); }
    if (!rendered) {
        return {{}, {}, 0.0,
            QStringLiteral("The melody could not be rendered safely. Check its notes and duration.")};
    }

    if (loop) {
        if (!loop->isValid()) { return {{}, {}, 0.0, QStringLiteral("Select a valid loop area.")}; }
        const auto first = static_cast<std::size_t>(std::llround(loop->start_seconds * config.sample_rate_hz));
        const auto last = static_cast<std::size_t>(std::llround(loop->end_seconds * config.sample_rate_hz));
        if (last * config.channel_count > config.maximum_interleaved_samples) {
            return {{}, {}, 0.0, QStringLiteral("This loop area is too long to render.")};
        }
        rendered->interleaved_samples.resize(std::max(rendered->interleaved_samples.size(), last * config.channel_count), 0.0F);
        const auto fade = std::max<std::size_t>(1, std::min<std::size_t>(config.sample_rate_hz / 500, (last - first) / 4));
        for (auto frame = first; frame < last; ++frame) {
            const auto gain = static_cast<float>(std::min({1.0, static_cast<double>(frame - first) / fade,
                static_cast<double>(last - frame - 1) / fade}));
            for (std::uint16_t channel = 0; channel < config.channel_count; ++channel) {
                rendered->interleaved_samples[frame * config.channel_count + channel] *= gain;
            }
        }
        rendered->phrase_duration_seconds = std::max(rendered->phrase_duration_seconds, loop->end_seconds);
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
            if (!playing_ && !preparing_ && !paused_) {
                return;
            }
            stop();
            report(QStringLiteral(
                "Melody playback stopped because the audio output changed. Press Play melody to restart."), true);
        });
    }

    void play(std::vector<core::TimedMidiNote> notes, QtMidiPlayback::StatusCallback callback,
        double start_seconds, double tempo_bpm, bool metronome, std::vector<core::InstrumentVoice> voices,
        std::optional<core::LoopRange> loop, std::optional<core::OverdubPlan> guide = std::nullopt, std::vector<core::VoiceClip> voice_clips = {}) {
        stop();
        if (loop && !loop->isValid()) { if (callback) { callback(QStringLiteral("Invalid loop area."), true); } return; }
        loop_range_ = loop;
        start_offset_seconds_ = std::isfinite(start_seconds) ? std::clamp(start_seconds, 0.0, 600.0) : 0.0;
        loop_ = loop.has_value() || (notes.empty() && voice_clips.empty() && !guide && metronome);
        if (loop && (start_offset_seconds_ < loop->start_seconds || start_offset_seconds_ >= loop->end_seconds)) {
            start_offset_seconds_ = loop->start_seconds;
        }
        status_callback_ = std::move(callback);
        output_device_ = QMediaDevices::defaultAudioOutput();
        if (output_device_.isNull()) {
            report(QStringLiteral(
                "No audio output is available. Connect speakers or headphones and try again."), true);
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
        report(QStringLiteral("Preparing instrument playback…"), false);
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
            [notes = std::move(notes), target_format, tempo_bpm, metronome, voices = std::move(voices), loop, guide = std::move(guide), voice_clips = std::move(voice_clips)] {
                return prepareMelody(notes, target_format, tempo_bpm, metronome, std::move(voices), loop, guide, voice_clips);
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
        paused_ = false;
        start_offset_seconds_ = 0.0;
        position_at_stop_seconds_ = 0.0;
        phrase_duration_seconds_ = 0.0;
        loop_range_.reset();
        manual_stop_ = false;
    }

    [[nodiscard]] bool isPlaying() const noexcept { return playing_; }
    [[nodiscard]] bool isPreparing() const noexcept { return preparing_; }
    [[nodiscard]] bool isPaused() const noexcept { return paused_; }

    void pause() {
        if (!playing_ || !sink_) { return; }
        position_at_stop_seconds_ = positionSeconds();
        sink_->suspend();
        playing_ = false;
        paused_ = true;
        report(QStringLiteral("Playback paused."), false);
    }

    void resume() {
        if (!paused_ || !sink_) { return; }
        paused_ = false;
        playing_ = true;
        sink_->resume();
        report(QStringLiteral("Playback resumed."), false);
    }

    void seek(double seconds) {
        if ((!playing_ && !paused_) || !sink_ || !buffer_ || !std::isfinite(seconds)) { return; }
        const auto paused = paused_;
        manual_stop_ = true;
        sink_->stop();
        start_offset_seconds_ = std::clamp(seconds, 0.0, loop_ ? 600.0 : phrase_duration_seconds_);
        if (loop_range_) { start_offset_seconds_ = loop_range_->wrap(start_offset_seconds_); }
        position_at_stop_seconds_ = start_offset_seconds_;
        buffer_->setOffset(static_cast<qint64>(std::llround(start_offset_seconds_ * format_.sampleRate()))
            * format_.bytesPerFrame());
        playing_ = true;
        paused_ = false;
        sink_->start(buffer_.get());
        manual_stop_ = false;
        if (sink_->error() != QAudio::NoError) {
            stop();
            report(QStringLiteral("The audio output could not seek. Press Play to retry."), true);
            return;
        }
        if (paused) { sink_->suspend(); playing_ = false; paused_ = true; }
    }

    [[nodiscard]] double positionSeconds() const noexcept {
        if (playing_ && sink_) {
            const auto position = start_offset_seconds_ + static_cast<double>(sink_->processedUSecs()) / 1'000'000.0;
            return loop_range_ ? loop_range_->wrap(position)
                : loop_ ? position : std::clamp(position, 0.0, phrase_duration_seconds_);
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
        format_ = prepared.format;
        phrase_duration_seconds_ = prepared.phrase_duration_seconds;
        if (!loop_) { start_offset_seconds_ = std::min(start_offset_seconds_, phrase_duration_seconds_); }
        position_at_stop_seconds_ = start_offset_seconds_;
        const auto first = loop_range_ ? static_cast<qint64>(std::llround(loop_range_->start_seconds * format_.sampleRate())) * format_.bytesPerFrame() : 0;
        const auto last = loop_range_ ? static_cast<qint64>(std::llround(loop_range_->end_seconds * format_.sampleRate())) * format_.bytesPerFrame() : -1;
        buffer_ = std::make_unique<MelodyBuffer>(bytes_, loop_, owner_, first, last);
        if (!buffer_->open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
            report(QStringLiteral("The in-memory melody buffer could not be opened."), true);
            return;
        }
        buffer_->setOffset(static_cast<qint64>(std::llround(start_offset_seconds_ * format_.sampleRate()))
            * format_.bytesPerFrame());
        sink_ = std::make_unique<QAudioSink>(output_device_, prepared.format, owner_);
        sink_->setBufferSize(prepared.format.bytesForDuration(100'000));
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
            if (state == QAudio::IdleState && sink_->error() == QAudio::NoError && buffer_ && buffer_->atEnd()) {
                playing_ = false;
                paused_ = false;
                position_at_stop_seconds_ = phrase_duration_seconds_;
                report(QStringLiteral("Melody playback complete."), false);
            } else if (state == QAudio::StoppedState && sink_->error() != QAudio::NoError) {
                playing_ = false;
                paused_ = false;
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
        report(QStringLiteral("Playing the arrangement…"), false);
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
    std::unique_ptr<MelodyBuffer> buffer_;
    QAudioFormat format_;
    QByteArray bytes_;
    QtMidiPlayback::StatusCallback status_callback_;
    std::uint64_t generation_{0};
    double phrase_duration_seconds_{0.0};
    double position_at_stop_seconds_{0.0};
    bool playing_{false};
    bool preparing_{false};
    bool manual_stop_{false};
    bool paused_{false};
    bool loop_{false};
    std::optional<core::LoopRange> loop_range_;
    double start_offset_seconds_{0.0};
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
    StatusCallback callback, double start_seconds, double tempo_bpm, bool metronome,
    std::vector<core::InstrumentVoice> voices, std::optional<core::LoopRange> loop, std::vector<core::VoiceClip> voice_clips) {
    impl_->play(std::move(notes), std::move(callback), start_seconds, tempo_bpm, metronome, std::move(voices), loop, std::nullopt, std::move(voice_clips));
}

void QtMidiPlayback::pause() { impl_->pause(); }
void QtMidiPlayback::playRecordingGuide(core::OverdubPlan plan, StatusCallback callback) {
    if (!plan.valid()) { if (callback) { callback(QStringLiteral("Invalid recording guide."), true); } return; }
    auto notes = plan.notes; auto voices = plan.voices; const auto bpm = plan.bpm;
    impl_->play(std::move(notes), std::move(callback), 0, bpm, false, std::move(voices), std::nullopt, std::move(plan));
}
void QtMidiPlayback::resume() { impl_->resume(); }
void QtMidiPlayback::seek(double seconds) { impl_->seek(seconds); }
bool QtMidiPlayback::isPaused() const noexcept { return impl_->isPaused(); }

void QtMidiPlayback::stop() noexcept { impl_->stop(); }
bool QtMidiPlayback::isPlaying() const noexcept { return impl_->isPlaying(); }
bool QtMidiPlayback::isPreparing() const noexcept { return impl_->isPreparing(); }
double QtMidiPlayback::positionSeconds() const noexcept { return impl_->positionSeconds(); }
double QtMidiPlayback::phraseDurationSeconds() const noexcept {
    return impl_->phraseDurationSeconds();
}

} // namespace hum::app
