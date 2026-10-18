#pragma once
#include <QAudioDevice>
#include <QAudioFormat>
#include <array>
#include <optional>

namespace hum::app {
// Some CoreAudio devices expose a preferred rate/channel count but Unknown sample
// format. Negotiate an explicitly supported PCM format before allocating audio.
template <typename Supports>
std::optional<QAudioFormat> chooseOutputFormat(const QAudioFormat& preferred, Supports supports) {
    const auto usable = [&](const QAudioFormat& format) {
        return format.isValid() && format.sampleRate() >= 8000 && format.sampleRate() <= 384000
            && format.channelCount() <= 8 && supports(format);
    };
    if (usable(preferred)) { return preferred; }
    for (const auto rate : {preferred.sampleRate(), 48000, 44100}) {
        for (const auto channels : {preferred.channelCount(), 2, 1}) {
            for (const auto sample : {QAudioFormat::Float, QAudioFormat::Int16,
                    QAudioFormat::Int32, QAudioFormat::UInt8}) {
                QAudioFormat format;
                format.setSampleRate(rate);
                format.setChannelCount(channels);
                format.setSampleFormat(sample);
                if (usable(format)) { return format; }
            }
        }
    }
    return std::nullopt;
}
inline std::optional<QAudioFormat> chooseOutputFormat(const QAudioDevice& device) {
    if (device.isNull()) { return std::nullopt; }
    return chooseOutputFormat(device.preferredFormat(), [&device](const auto& format) {
        return device.isFormatSupported(format);
    });
}
} // namespace hum::app
