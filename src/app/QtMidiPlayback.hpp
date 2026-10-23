#pragma once

#include "hum/core/midi.hpp"
#include "hum/core/note_editor.hpp"
#include "hum/core/loop_range.hpp"
#include "hum/core/overdub.hpp"

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

    void play(std::vector<core::TimedMidiNote> notes, StatusCallback callback,
        double start_seconds = 0.0, double tempo_bpm = 120.0, bool metronome = false,
        std::vector<core::InstrumentVoice> voices = {}, std::optional<core::LoopRange> loop = std::nullopt,
        std::vector<core::VoiceClip> voice_clips = {});
    void pause();
    void playRecordingGuide(core::OverdubPlan plan, StatusCallback callback);
    void resume();
    void seek(double seconds);
    [[nodiscard]] bool isPaused() const noexcept;
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
