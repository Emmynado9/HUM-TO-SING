#pragma once
#include "hum/core/audio.hpp"
#include "hum/vocal/transcription.hpp"
#include <memory>

namespace hum::app::vocal {

// Owns a session, never the old recorder's controller or samples. Capture is an
// injected device interface so cancellation/disconnection can be tested alone.
class IsolatedMicRecorder final {
public:
    explicit IsolatedMicRecorder(core::IAudioCapture& capture, double limit_seconds = 30);
    ~IsolatedMicRecorder();
    [[nodiscard]] core::ServiceStatus start(const std::string& device);
    void stop();
    void discard();
    void poll();
    [[nodiscard]] bool recording() const { return recording_; }
    [[nodiscard]] bool limitReached() const;
    [[nodiscard]] core::ServiceStatus error() const;
    [[nodiscard]] double elapsed() const;
    [[nodiscard]] float peak() const;
    [[nodiscard]] bool clipped() const;
    [[nodiscard]] std::shared_ptr<const hum::vocal::AudioBuffer> audio() const { return audio_; }
private:
    struct Session;
    core::IAudioCapture& capture_;
    double limit_;
    bool recording_{false};
    std::shared_ptr<Session> session_;
    std::shared_ptr<const hum::vocal::AudioBuffer> audio_;
};
}
