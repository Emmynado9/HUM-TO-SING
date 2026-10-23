#pragma once
#include "QtMidiPlayback.hpp"
#include "hum/core/overdub.hpp"
#include <QTimer>

namespace hum::app {
struct RecordingGuideCallbacks {
    std::function<void(double origin)> capture;
    std::function<void(int beat, double position)> position;
    std::function<void(const QString& error)> finished;
};
class IRecordingGuide {
public:
    virtual ~IRecordingGuide() = default;
    virtual void start(core::OverdubPlan plan, RecordingGuideCallbacks callbacks) = 0;
    virtual void stop() = 0;
    virtual bool active() const = 0;
};
class RecordingGuidePlayer final : public QObject, public IRecordingGuide {
public:
    explicit RecordingGuidePlayer(QObject* parent = nullptr);
    ~RecordingGuidePlayer() override { stop(); }
    void start(core::OverdubPlan plan, RecordingGuideCallbacks callbacks) override;
    void stop() override;
    bool active() const override { return active_; }
private:
    void poll();
    void finish(const QString& error);
    QtMidiPlayback playback_;
    QTimer timer_;
    core::OverdubPlan plan_;
    RecordingGuideCallbacks callbacks_;
    bool active_{false}, capturing_{false};
    std::uint64_t generation_{0};
};
}
