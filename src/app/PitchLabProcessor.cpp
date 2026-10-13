#include "PitchLabProcessor.hpp"

#include <QFutureWatcher>
#include <QtConcurrentRun>

#include <chrono>
#include <memory>
#include <utility>

namespace hum::app {

class PitchLabProcessor::Impl final
    : public std::enable_shared_from_this<PitchLabProcessor::Impl> {
public:
    explicit Impl(PitchLabProcessor* owner)
        : owner_(owner),
          detector_(std::make_shared<core::YinPitchDetector>()) {
    }

    bool analyze(
        std::vector<PitchFrameJob> frames,
        const std::uint32_t sample_rate_hz,
        const core::PitchDetectorConfig config,
        PitchLabProcessor::LiveCallback callback) {
        if (busy_ || frames.empty()) {
            return false;
        }
        busy_ = true;
        const auto generation = generation_;
        const auto detector = detector_;
        const auto weak_impl = weak_from_this();
        auto* watcher = new QFutureWatcher<LivePitchBatchResult>(owner_);
        QObject::connect(watcher, &QFutureWatcher<LivePitchBatchResult>::finished, owner_,
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
            [detector, frames = std::move(frames), sample_rate_hz, config]() mutable {
                const auto started = std::chrono::steady_clock::now();
                LivePitchBatchResult result;
                result.estimates.reserve(frames.size());
                for (const auto& frame : frames) {
                    result.estimates.push_back(detector->estimate(
                        frame.mono_samples,
                        sample_rate_hz,
                        config,
                        frame.frame_start_sample));
                }
                result.processing_milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                return result;
            }));
        return true;
    }

    bool replay(
        std::shared_ptr<const core::AudioRecording> recording,
        const core::PitchDetectorConfig config,
        PitchLabProcessor::ReplayCallback callback) {
        if (busy_ || !recording) {
            return false;
        }
        busy_ = true;
        const auto generation = generation_;
        const auto detector = detector_;
        const auto weak_impl = weak_from_this();
        auto* watcher = new QFutureWatcher<ReplayPitchResult>(owner_);
        QObject::connect(watcher, &QFutureWatcher<ReplayPitchResult>::finished, owner_,
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
            [detector, recording = std::move(recording), config] {
                const auto started = std::chrono::steady_clock::now();
                const core::PitchAnalysisRunner runner(detector);
                ReplayPitchResult result;
                result.estimates = runner.analyze(*recording, config);
                result.processing_milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                return result;
            }));
        return true;
    }

    void cancel() noexcept {
        ++generation_;
        busy_ = false;
    }

    [[nodiscard]] bool isBusy() const noexcept {
        return busy_;
    }

private:
    PitchLabProcessor* owner_;
    std::shared_ptr<const core::IPitchDetector> detector_;
    std::uint64_t generation_{0};
    bool busy_{false};
};

PitchLabProcessor::PitchLabProcessor(QObject* parent)
    : QObject(parent),
      impl_(std::make_shared<Impl>(this)) {
}

PitchLabProcessor::~PitchLabProcessor() {
    cancel();
}

bool PitchLabProcessor::analyze(
    std::vector<PitchFrameJob> frames,
    const std::uint32_t sample_rate_hz,
    core::PitchDetectorConfig config,
    LiveCallback callback) {
    return impl_->analyze(
        std::move(frames), sample_rate_hz, std::move(config), std::move(callback));
}

bool PitchLabProcessor::replay(
    std::shared_ptr<const core::AudioRecording> recording,
    core::PitchDetectorConfig config,
    ReplayCallback callback) {
    return impl_->replay(std::move(recording), std::move(config), std::move(callback));
}

void PitchLabProcessor::cancel() noexcept {
    impl_->cancel();
}

bool PitchLabProcessor::isBusy() const noexcept {
    return impl_->isBusy();
}

} // namespace hum::app
