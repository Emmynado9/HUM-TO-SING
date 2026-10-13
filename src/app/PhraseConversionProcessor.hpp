#pragma once

#include "hum/core/note_extraction.hpp"

#include <QObject>

#include <functional>
#include <memory>

namespace hum::app {

struct PhraseWorkerResult {
    core::PhraseConversionResult conversion;
    double processing_milliseconds{0.0};
};

class PhraseConversionProcessor final : public QObject {
public:
    using Callback = std::function<void(PhraseWorkerResult)>;

    explicit PhraseConversionProcessor(QObject* parent = nullptr);
    ~PhraseConversionProcessor() override;

    [[nodiscard]] bool convert(
        std::shared_ptr<const core::AudioRecording> recording,
        core::PitchDetectorConfig pitch_config,
        core::NoteExtractionConfig extraction_config,
        double tempo_bpm,
        std::uint16_t ticks_per_quarter,
        Callback callback);
    void cancel() noexcept;
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] double progress() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace hum::app
