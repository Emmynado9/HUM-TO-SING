#include "VocalPreviewWidget.hpp"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace hum::app::vocal {
VocalPreviewWidget::VocalPreviewWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("vocalPreview")); setMinimumHeight(200);
    setAccessibleName(QStringLiteral("Read-only vocal note preview"));
    setToolTip(QStringLiteral("Blue notes are the temporary vocal preview. Grey notes already belong to the selected track."));
}
void VocalPreviewWidget::setNotes(std::vector<hum::vocal::NoteEvent> notes,
    std::vector<hum::vocal::NoteEvent> reference, double origin, double bpm) {
    notes_ = std::move(notes); reference_ = std::move(reference); origin_ = origin; bpm_ = bpm; update();
}
void VocalPreviewWidget::setPlayhead(double seconds) { playhead_ = seconds; update(); }
void VocalPreviewWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#f7fafb"));
    if (notes_.empty()) {
        painter.setPen(QColor("#64717d"));
        painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
            QStringLiteral("Your converted melody will appear here.\nPreviewing does not add or change any track notes.")); return;
    }
    double duration = 1;
    int low = 127, high = 0;
    for (const auto& note : notes_) { duration = std::max(duration, note.start + note.duration); low = std::min(low, note.pitch); high = std::max(high, note.pitch); }
    low = std::max(0, low - 3); high = std::min(127, std::max(high + 3, low + 11));
    const double left = 48, top = 25, bottom = 25;
    const auto width = std::max(1.0, this->width() - left - 12);
    const auto row = std::max(1.0, (height() - top - bottom) / (high - low + 1));
    const auto x = [&](double t) { return left + t / duration * width; };
    for (int pitch = low; pitch <= high; ++pitch) {
        const auto y = top + (high - pitch) * row;
        painter.setPen(QColor("#e0e6e9")); painter.drawLine(QPointF(left, y), QPointF(left + width, y));
        if (pitch % 12 == 0 || pitch == low || pitch == high) {
            painter.setPen(QColor("#627080")); painter.drawText(QRectF(0, y, left - 5, row), Qt::AlignRight | Qt::AlignVCenter, QString::number(pitch));
        }
    }
    const auto interval = std::max(60.0 / bpm_, duration / 12);
    for (auto time = std::ceil(origin_ / interval) * interval; time <= origin_ + duration; time += interval) {
        const auto position = x(time - origin_);
        painter.setPen(QColor("#d9e3e8")); painter.drawLine(QPointF(position, top), QPointF(position, height() - bottom));
        painter.setPen(QColor("#52616f")); painter.drawText(QRectF(position + 3, 0, 65, top), Qt::AlignVCenter, QStringLiteral("%1 s").arg(time, 0, 'f', 1));
    }
    painter.save(); painter.setClipRect(QRectF(left, top, width, height() - top - bottom));
    const auto draw = [&](const auto& notes, QColor color, bool absolute) {
        painter.setPen(color.darker(115)); painter.setBrush(color);
        for (const auto& note : notes) {
            const auto start = note.start - (absolute ? origin_ : 0);
            if (start + note.duration < 0 || start > duration || note.pitch < low || note.pitch > high) { continue; }
            painter.drawRoundedRect(QRectF(x(start), top + (high - note.pitch) * row + 1,
                std::max(2.0, note.duration / duration * width), std::max(2.0, row - 2)), 3, 3);
        }
    };
    draw(reference_, QColor("#cbd2d6"), true); draw(notes_, QColor("#55bce0"), false);
    if (playhead_ >= 0) { painter.setPen(QPen(QColor("#c25b36"), 2)); painter.drawLine(QPointF(x(playhead_), top), QPointF(x(playhead_), height() - bottom)); }
    painter.restore(); painter.setPen(QColor("#52616f"));
    painter.drawText(QRectF(left, height() - bottom, width, bottom), Qt::AlignVCenter,
        QStringLiteral("PREVIEW · %1 notes · blue: this take · grey: existing track").arg(notes_.size()));
}
}
