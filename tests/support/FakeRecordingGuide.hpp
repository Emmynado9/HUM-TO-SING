#pragma once
#include "RecordingGuidePlayer.hpp"
namespace hum::test {
class FakeRecordingGuide final : public app::IRecordingGuide {
public:
    core::OverdubPlan plan;
    app::RecordingGuideCallbacks callbacks;
    bool running{false}, capturing{false};
    void start(core::OverdubPlan value, app::RecordingGuideCallbacks handlers) override {
        stop(); plan = std::move(value); callbacks = std::move(handlers); running = true;
    }
    void stop() override { running = false; capturing = false; callbacks = {}; }
    bool active() const override { return running; }
    void advance(double seconds) {
        if (!running) { return; }
        if (!capturing && seconds >= plan.countInSeconds()) {
            capturing = true; const auto callback = callbacks.capture;
            if (callback) { callback(plan.projectPosition(seconds)); }
        }
        const auto callback = callbacks.position;
        if (running && callback) { callback(plan.countInBeat(seconds), plan.projectPosition(seconds)); }
    }
    void fail() {
        const auto callback = callbacks.finished; stop();
        if (callback) { callback(QStringLiteral("Simulated output failure")); }
    }
};
}
