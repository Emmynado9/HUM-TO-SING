#pragma once

#include "hum/core/recording.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace hum::app {

class QtAudioPlayback final : public QObject {
public:
    using StatusCallback = std::function<void(const QString& message, bool is_error)>;

    explicit QtAudioPlayback(QObject* parent = nullptr);
    ~QtAudioPlayback() override;

    void play(std::shared_ptr<const core::AudioRecording> recording, StatusCallback callback);
    void stop() noexcept;
    [[nodiscard]] bool isPlaying() const noexcept;
    [[nodiscard]] bool isPreparing() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace hum::app
