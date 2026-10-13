#include "LevelMeter.hpp"

#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QRectF>

#include <algorithm>

namespace hum::app {

LevelMeter::LevelMeter(QWidget* parent)
    : QWidget(parent) {
    setAccessibleName(QStringLiteral("Microphone input level"));
    setToolTip(QStringLiteral("Live microphone peak level; the red zone indicates clipping."));
}

void LevelMeter::setLevel(const float linear_peak, const bool clipped) {
    level_ = std::clamp(linear_peak, 0.0F, 1.0F);
    clipped_ = clipped;
    setAccessibleDescription(QStringLiteral("Input level %1 percent%2")
        .arg(static_cast<int>(level_ * 100.0F))
        .arg(clipped_ ? QStringLiteral(", clipping detected") : QString{}));
    update();
}

QSize LevelMeter::minimumSizeHint() const {
    return {260, 24};
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF track = rect().adjusted(1, 1, -1, -1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#dedede")));
    painter.drawRoundedRect(track, 8.0, 8.0);

    auto fill = track;
    fill.setWidth(track.width() * level_);
    painter.setBrush(level_ >= 0.98F
        ? QColor(QStringLiteral("#e34848"))
        : QColor(QStringLiteral("#111111")));
    painter.drawRoundedRect(fill, 8.0, 8.0);

    auto clip_zone = track;
    clip_zone.setLeft(track.left() + track.width() * 0.98);
    painter.setBrush(clipped_
        ? QColor(QStringLiteral("#e34848"))
        : QColor(QStringLiteral("#c5c5c5")));
    painter.drawRoundedRect(clip_zone, 3.0, 3.0);
}

} // namespace hum::app
