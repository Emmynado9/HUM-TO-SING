#pragma once

#include "hum/core/midi.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace hum::app {

class QtMidiPlayback final : public QObject {
public:
    using StatusCallback = std::function<void(const QString& message, bool is_error)>;

    explicit QtMidiPlayback(QObject* parent = nullptr);
    ~QtMidiPlayback() override;

    void play(std::vector<core::TimedMidiNote> notes, StatusCallback callback);
    void stop() noexcept;
    [[nodiscard]] bool isPlaying() const noexcept;
    [[nodiscard]] bool isPreparing() const noexcept;
    [[nodiscard]] double positionSeconds() const noexcept;
    [[nodiscard]] double phraseDurationSeconds() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace hum::app
