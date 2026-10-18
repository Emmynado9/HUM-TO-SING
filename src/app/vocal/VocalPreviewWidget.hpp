#pragma once
#include "hum/vocal/transcription.hpp"
#include <QWidget>

namespace hum::app::vocal {
class VocalPreviewWidget final : public QWidget {
public:
    explicit VocalPreviewWidget(QWidget* parent = nullptr);
    void setNotes(std::vector<hum::vocal::NoteEvent> notes,
        std::vector<hum::vocal::NoteEvent> reference, double origin, double bpm);
    void setPlayhead(double seconds);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<hum::vocal::NoteEvent> notes_, reference_;
    double origin_{0}, bpm_{120}, playhead_{-1};
};
}
