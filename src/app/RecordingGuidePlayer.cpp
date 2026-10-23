#include "RecordingGuidePlayer.hpp"
#include <QMetaObject>

namespace hum::app {
RecordingGuidePlayer::RecordingGuidePlayer(QObject* parent) : QObject(parent), playback_(this) {
    timer_.setInterval(5); timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}
void RecordingGuidePlayer::start(core::OverdubPlan plan, RecordingGuideCallbacks callbacks) {
    stop(); plan_ = std::move(plan); callbacks_ = std::move(callbacks); active_ = true;
    if (!plan_.valid()) { finish(QStringLiteral("Choose a valid recording position and tempo.")); return; }
    const auto generation = generation_;
    playback_.playRecordingGuide(plan_, [this, generation](const QString& message, bool error) {
        // Do not destroy the audio sink from inside its own stateChanged callback.
        if (error) { QMetaObject::invokeMethod(this, [this, generation, message] {
            if (generation == generation_ && active_) { finish(message); }
        }, Qt::QueuedConnection); }
    });
    timer_.start();
}
void RecordingGuidePlayer::stop() {
    ++generation_; timer_.stop(); active_ = false; capturing_ = false;
    callbacks_ = {}; playback_.stop();
}
void RecordingGuidePlayer::finish(const QString& error) {
    const auto callback = callbacks_.finished; stop(); if (callback) { callback(error); }
}
void RecordingGuidePlayer::poll() {
    if (!active_ || playback_.isPreparing()) { return; }
    const auto elapsed = playback_.positionSeconds();
    if (elapsed >= plan_.countInSeconds() + plan_.duration || !playback_.isPlaying()) { finish({}); return; }
    if (!capturing_ && elapsed >= plan_.countInSeconds()) {
        capturing_ = true;
        const auto callback = callbacks_.capture;
        // Account for timer scheduling delay in the inserted take's origin.
        if (callback) { callback(plan_.projectPosition(elapsed)); }
        if (!active_) { return; }
    }
    const auto callback = callbacks_.position;
    if (callback) { callback(plan_.countInBeat(elapsed), plan_.projectPosition(elapsed)); }
}
}
