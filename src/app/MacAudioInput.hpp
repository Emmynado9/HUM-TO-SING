#pragma once

#include "hum/core/audio.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace hum::app {

class MacAudioInput final {
public:
    ~MacAudioInput();
    [[nodiscard]] std::vector<core::AudioInputDevice> devices() const;
    [[nodiscard]] std::optional<core::AudioFormat> preferredFormat(const std::string& id) const;
    core::ServiceStatus start(const std::string& id, core::AudioFormat format,
        core::AudioBlockCallback callback, core::CaptureFailureCallback failure);
    void stop() noexcept;
    [[nodiscard]] bool isCapturing() const noexcept;

private:
    static void receive(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer,
        const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*);
    AudioQueueRef queue_{nullptr};
    std::atomic<bool> running_{false};
    core::AudioFormat format_{};
    core::AudioBlockCallback callback_;
    core::CaptureFailureCallback failure_;
};

}
