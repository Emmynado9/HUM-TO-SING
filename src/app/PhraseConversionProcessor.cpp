#include "PhraseConversionProcessor.hpp"

#include <QFutureWatcher>
#include <QtConcurrentRun>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

namespace hum::app {

class PhraseConversionProcessor::Impl final
    : public std::enable_shared_from_this<PhraseConversionProcessor::Impl> {
public:
    explicit Impl(PhraseConversionProcessor* owner)
        : owner_(owner),
          detector_(std::make_shared<core::YinPitchDetector>()),
          segmenter_(std::make_shared<core::HysteresisNoteSegmenter>()) {
    }

    bool convert(
        std::shared_ptr<const core::AudioRecording> recording,
        const core::PitchDetectorConfig pitch_config,
        const core::NoteExtractionConfig extraction_config,
        const double tempo_bpm,
        const std::uint16_t ticks_per_quarter,
        PhraseConversionProcessor::Callback callback) {
        if (busy_ || !recording) {
            return false;
        }
        busy_ = true;
        progress_->store(0.0, std::memory_order_relaxed);
        cancel_token_ = std::make_shared<std::atomic_bool>(false);
        const auto token = cancel_token_;
        const auto progress = progress_;
        const auto detector = detector_;
        const auto segmenter = segmenter_;
        const auto generation = generation_;
        const auto weak_impl = weak_from_this();
        auto* watcher = new QFutureWatcher<PhraseWorkerResult>(owner_);
        QObject::connect(watcher, &QFutureWatcher<PhraseWorkerResult>::finished, owner_,
            [weak_impl, watcher, generation, callback = std::move(callback)]() mutable {
                auto result = watcher->result();
                watcher->deleteLater();
                const auto impl = weak_impl.lock();
                if (!impl || generation != impl->generation_) {
                    return;
                }
                impl->busy_ = false;
                if (callback) {
                    callback(std::move(result));
                }
            });
        watcher->setFuture(QtConcurrent::run(
            [recording = std::move(recording), pitch_config, extraction_config,
                tempo_bpm, ticks_per_quarter, detector, segmenter, token, progress] {
                const auto started = std::chrono::steady_clock::now();
                const core::PhraseConversionPipeline pipeline(detector, segmenter);
                PhraseWorkerResult result;
                result.conversion = pipeline.convert(
                    *recording,
                    pitch_config,
                    extraction_config,
                    tempo_bpm,
                    ticks_per_quarter,
                    [progress](const double fraction) {
                        progress->store(fraction, std::memory_order_relaxed);
                    },
                    [token] { return token->load(std::memory_order_relaxed); });
                result.processing_milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                return result;
            }));
        return true;
    }

    void cancel() noexcept {
        if (cancel_token_) {
            cancel_token_->store(true, std::memory_order_relaxed);
        }
        ++generation_;
        busy_ = false;
    }

    [[nodiscard]] bool isBusy() const noexcept {
        return busy_;
    }

    [[nodiscard]] double progress() const noexcept {
        return progress_->load(std::memory_order_relaxed);
    }

private:
    PhraseConversionProcessor* owner_;
    std::shared_ptr<const core::IPitchDetector> detector_;
    std::shared_ptr<const core::INoteSegmenter> segmenter_;
    std::shared_ptr<std::atomic<double>> progress_{std::make_shared<std::atomic<double>>(0.0)};
    std::shared_ptr<std::atomic_bool> cancel_token_;
    std::uint64_t generation_{0};
    bool busy_{false};
};

PhraseConversionProcessor::PhraseConversionProcessor(QObject* parent)
    : QObject(parent),
      impl_(std::make_shared<Impl>(this)) {
}

PhraseConversionProcessor::~PhraseConversionProcessor() {
    cancel();
}

bool PhraseConversionProcessor::convert(
    std::shared_ptr<const core::AudioRecording> recording,
    core::PitchDetectorConfig pitch_config,
    core::NoteExtractionConfig extraction_config,
    const double tempo_bpm,
    const std::uint16_t ticks_per_quarter,
    Callback callback) {
    return impl_->convert(
        std::move(recording),
        std::move(pitch_config),
        std::move(extraction_config),
        tempo_bpm,
        ticks_per_quarter,
        std::move(callback));
}

void PhraseConversionProcessor::cancel() noexcept {
    impl_->cancel();
}

bool PhraseConversionProcessor::isBusy() const noexcept {
    return impl_->isBusy();
}

double PhraseConversionProcessor::progress() const noexcept {
    return impl_->progress();
}

} // namespace hum::app
