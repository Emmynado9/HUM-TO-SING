#include "PianoRollWidget.hpp"

#include "hum/core/pitch.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace hum::app {

namespace {

constexpr int kPitchAxisWidth = 72;
constexpr int kTimeAxisHeight = 28;
constexpr double kMinimumUiDurationSeconds = 0.020;
constexpr double kDefaultAddedDurationSeconds = 0.500;
constexpr double kMinimumTimelineSeconds = 8.0;

[[nodiscard]] bool isBlackKey(const int midi_pitch) noexcept {
    constexpr std::array<bool, 12> black{
        false, true, false, true, false, false,
        true, false, true, false, true, false};
    return black[static_cast<std::size_t>(midi_pitch % 12)];
}

[[nodiscard]] double clampedHorizontalZoom(const double value) noexcept {
    return std::clamp(value, 50.0, 520.0);
}

[[nodiscard]] double clampedVerticalZoom(const double value) noexcept {
    return std::clamp(value, 10.0, 34.0);
}

} // namespace

PianoRollWidget::PianoRollWidget(core::NoteEditor& editor, QWidget* parent)
    : QAbstractScrollArea(parent),
      editor_(editor) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->setCursor(Qt::CrossCursor);
    setAccessibleName(QStringLiteral("Editable melody piano roll"));
    setAccessibleDescription(QStringLiteral(
        "Click a piano key to hear it. Time runs left to right and pitch runs bottom to top. Double-click empty space to add a note."));
    horizontalScrollBar()->setSingleStep(40);
    verticalScrollBar()->setSingleStep(20);
    updateScrollBars();
}

void PianoRollWidget::setDocumentChangedCallback(std::function<void()> callback) {
    document_changed_callback_ = std::move(callback);
}

void PianoRollWidget::setSelectionChangedCallback(std::function<void()> callback) {
    selection_changed_callback_ = std::move(callback);
}

void PianoRollWidget::setStatusCallback(std::function<void(const QString&)> callback) {
    status_callback_ = std::move(callback);
}

void PianoRollWidget::setHorizontalZoom(const double pixels_per_second) {
    transform_.pixels_per_second = clampedHorizontalZoom(pixels_per_second);
    updateScrollBars();
    viewport()->update();
}

void PianoRollWidget::setVerticalZoom(const double pixels_per_semitone) {
    const auto center_pitch = transform_.yToPitch(
        static_cast<double>(verticalScrollBar()->value())
        + std::max(0, viewport()->height() - kTimeAxisHeight) / 2.0);
    transform_.pixels_per_semitone = clampedVerticalZoom(pixels_per_semitone);
    updateScrollBars();
    if (center_pitch) {
        const auto desired = transform_.pitchToY(*center_pitch)
            - std::max(0, viewport()->height() - kTimeAxisHeight) / 2.0;
        verticalScrollBar()->setValue(static_cast<int>(std::lround(desired)));
    }
    viewport()->update();
}

void PianoRollWidget::setRhythmSnap(const core::RhythmSnap snap) {
    if (snap.isValid()) {
        rhythm_snap_ = snap;
        viewport()->update();
    }
}

void PianoRollWidget::setPlayheadSeconds(
    const std::optional<double> seconds,
    const bool follow) {
    if (seconds && (!std::isfinite(*seconds) || *seconds < 0.0)) {
        return;
    }
    playhead_seconds_ = seconds;
    if (follow && seconds) {
        const auto content_x = transform_.timeToX(*seconds);
        const auto grid_width = std::max(0, viewport()->width() - kPitchAxisWidth);
        const auto visible_left = static_cast<double>(horizontalScrollBar()->value());
        const auto visible_right = visible_left + grid_width;
        if (content_x < visible_left || content_x > visible_right - 20.0) {
            const auto desired = std::max(0.0, content_x - grid_width * 0.15);
            horizontalScrollBar()->setValue(static_cast<int>(std::lround(desired)));
        }
    }
    viewport()->update();
}

core::PianoRollTransform PianoRollWidget::transform() const noexcept {
    return transform_;
}

core::RhythmSnap PianoRollWidget::rhythmSnap() const noexcept {
    return rhythm_snap_;
}

std::optional<double> PianoRollWidget::playheadViewportX() const noexcept {
    if (!playhead_seconds_) {
        return std::nullopt;
    }
    return kPitchAxisWidth + transform_.timeToX(*playhead_seconds_)
        - horizontalScrollBar()->value();
}

void PianoRollWidget::refresh(const bool focus_notes) {
    drag_mode_ = DragMode::none;
    drag_preview_.reset();
    drag_origin_pitch_.reset();
    drag_display_original_pitch_.reset();
    marquee_.reset();
    updateScrollBars();
    if (focus_notes) {
        focusNotes();
    }
    viewport()->update();
}

void PianoRollWidget::focusNotes() {
    if (editor_.projectedNotes().empty()) {
        verticalScrollBar()->setValue(static_cast<int>(std::lround(
            transform_.pitchToY(60) - viewport()->height() / 2.0)));
        horizontalScrollBar()->setValue(0);
        return;
    }
    auto minimum_pitch = 127;
    auto maximum_pitch = 0;
    double first_start = std::numeric_limits<double>::max();
    if (const auto selected = editor_.selectedIndex()) {
        const auto& note = editor_.projectedNotes()[*selected];
        minimum_pitch = maximum_pitch = note.pitch;
        first_start = note.start_seconds;
    } else {
        for (const auto& note : editor_.projectedNotes()) {
            minimum_pitch = std::min(minimum_pitch, static_cast<int>(note.pitch));
            maximum_pitch = std::max(maximum_pitch, static_cast<int>(note.pitch));
            first_start = std::min(first_start, note.start_seconds);
        }
    }
    const auto center_pitch = static_cast<std::uint8_t>((minimum_pitch + maximum_pitch) / 2);
    verticalScrollBar()->setValue(static_cast<int>(std::lround(
        transform_.pitchToY(center_pitch)
        - std::max(0, viewport()->height() - kTimeAxisHeight) / 2.0)));
    horizontalScrollBar()->setValue(static_cast<int>(std::lround(
        std::max(0.0, transform_.timeToX(first_start) - 30.0))));
}

void PianoRollWidget::setTool(const Tool tool) {
    tool_ = tool;
    drag_mode_ = DragMode::none;
    drag_preview_.reset();
    marquee_.reset();
    viewport()->setCursor(tool == Tool::select ? Qt::ArrowCursor
        : tool == Tool::resize ? Qt::SizeHorCursor : Qt::CrossCursor);
    viewport()->update();
}

PianoRollWidget::Tool PianoRollWidget::tool() const noexcept { return tool_; }

void PianoRollWidget::setTempo(const double bpm) {
    if (std::isfinite(bpm) && bpm >= 20 && bpm <= 400) { tempo_bpm_ = bpm; viewport()->update(); }
}

void PianoRollWidget::addNoteAtViewportCenter() {
    const auto content_time = transform_.xToTime(
        static_cast<double>(horizontalScrollBar()->value())
        + std::max(0, viewport()->width() - kPitchAxisWidth) / 2.0);
    const auto center_y = static_cast<double>(verticalScrollBar()->value())
        + std::max(0, viewport()->height() - kTimeAxisHeight) / 2.0;
    const auto pitch = transform_.yToPitch(center_y).value_or(60);
    const auto result = editor_.add({
        pitch,
        96,
        rhythm_snap_.apply(content_time),
        kDefaultAddedDurationSeconds,
        0,
    });
    showEditResult(result, QStringLiteral("Added %1.").arg(
        QString::fromStdString(core::midiNoteName(pitch))));
}

void PianoRollWidget::deleteSelected() {
    showEditResult(editor_.deleteSelected(), QStringLiteral("Deleted the selected note."));
}

void PianoRollWidget::undo() {
    const auto label = QString::fromStdString(editor_.undoLabel());
    if (editor_.undo()) {
        notifySelectionChanged();
        notifyDocumentChanged(QStringLiteral("Undid %1.").arg(label.toLower()));
    } else if (status_callback_) {
        status_callback_(QStringLiteral("There is nothing to undo."));
    }
}

void PianoRollWidget::redo() {
    const auto label = QString::fromStdString(editor_.redoLabel());
    if (editor_.redo()) {
        notifySelectionChanged();
        notifyDocumentChanged(QStringLiteral("Redid %1.").arg(label.toLower()));
    } else if (status_callback_) {
        status_callback_(QStringLiteral("There is nothing to redo."));
    }
}

QRectF PianoRollWidget::noteViewportRect(const std::size_t index) const {
    if (index >= editor_.projectedNotes().size()) {
        return {};
    }
    auto note = editor_.projectedNotes()[index];
    if (drag_preview_ && editor_.isSelected(index)) {
        note = editor_.notes()[index];
        if (drag_mode_ == DragMode::move) {
            note.start_seconds += drag_preview_->start_seconds - drag_original_.start_seconds;
            note.pitch = static_cast<std::uint8_t>(std::clamp(static_cast<int>(note.pitch)
                + static_cast<int>(drag_preview_->pitch) - static_cast<int>(drag_original_.pitch), 0, 127));
        } else if (drag_index_ == index) {
            note = *drag_preview_;
        }
        note.pitch = editor_.projectedPitch(note.pitch);
    }
    return {
        kPitchAxisWidth + transform_.timeToX(note.start_seconds)
            - horizontalScrollBar()->value(),
        kTimeAxisHeight + transform_.pitchToY(note.pitch)
            - verticalScrollBar()->value() + 1.0,
        std::max(3.0, transform_.timeToX(note.duration_seconds)),
        std::max(2.0, transform_.pixels_per_semitone - 2.0),
    };
}

void PianoRollWidget::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(viewport()->rect(), QColor(QStringLiteral("#ffffff")));

    const auto grid_left = kPitchAxisWidth;
    const auto grid_top = kTimeAxisHeight;
    const auto grid_width = std::max(0, viewport()->width() - grid_left);
    const auto grid_height = std::max(0, viewport()->height() - grid_top);
    painter.save();
    painter.setClipRect(grid_left, grid_top, grid_width, grid_height);
    for (int pitch = 127; pitch >= 0; --pitch) {
        const auto row_y = grid_top + transform_.pitchToY(static_cast<std::uint8_t>(pitch))
            - verticalScrollBar()->value();
        if (row_y + transform_.pixels_per_semitone < grid_top || row_y > viewport()->height()) {
            continue;
        }
        const auto in_scale = editor_.pitchIsInScale(static_cast<std::uint8_t>(pitch));
        painter.fillRect(
            QRectF(grid_left, row_y, grid_width, transform_.pixels_per_semitone),
            in_scale ? QColor(QStringLiteral("#f2f6f2"))
                     : (isBlackKey(pitch) ? QColor(QStringLiteral("#eeeeef"))
                                          : QColor(QStringLiteral("#fafafa"))));
        painter.setPen(QColor(QStringLiteral("#dddddf")));
        painter.drawLine(QPointF(grid_left, row_y), QPointF(viewport()->width(), row_y));
    }

    const auto visible_start = transform_.xToTime(horizontalScrollBar()->value());
    const auto visible_end = transform_.xToTime(
        static_cast<double>(horizontalScrollBar()->value()) + grid_width);
    const auto beat_seconds = 60.0 / tempo_bpm_;
    double minor_step = rhythm_snap_.enabled ? rhythm_snap_.interval_seconds : beat_seconds / 4.0;
    while (transform_.timeToX(minor_step) < 24.0) {
        minor_step *= 2.0;
    }
    const auto first_grid = std::floor(visible_start / minor_step) * minor_step;
    for (auto time = first_grid; time <= visible_end + minor_step; time += minor_step) {
        const auto x = grid_left + transform_.timeToX(time)
            - horizontalScrollBar()->value();
        const auto beat_index = static_cast<long long>(std::llround(time / beat_seconds));
        const auto is_beat = std::abs(time - static_cast<double>(beat_index) * beat_seconds) < 1.0e-6;
        painter.setPen(is_beat ? QColor(QStringLiteral("#bcbcc0"))
                               : QColor(QStringLiteral("#dedee1")));
        painter.drawLine(QPointF(x, grid_top), QPointF(x, viewport()->height()));
    }

    for (std::size_t index = 0; index < editor_.notes().size(); ++index) {
        const auto rect = noteViewportRect(index);
        if (!rect.intersects(QRectF(grid_left, grid_top, grid_width, grid_height))) {
            continue;
        }
        const auto selected = editor_.isSelected(index);
        const auto canonical = drag_preview_ && drag_index_ == index
            ? *drag_preview_ : editor_.notes()[index];
        const auto in_scale = editor_.pitchIsInScale(canonical.pitch);
        painter.setBrush(selected ? QColor(QStringLiteral("#090909"))
            : (in_scale ? QColor(QStringLiteral("#4f6255"))
                        : QColor(QStringLiteral("#b4b4b8"))));
        QPen note_pen(selected ? QColor(QStringLiteral("#090909"))
                               : QColor(QStringLiteral("#4a4a4d")));
        if (!in_scale) {
            note_pen.setStyle(Qt::DashLine);
        }
        painter.setPen(note_pen);
        if (canonical.muted) {
            painter.setBrush(QColor(QStringLiteral("#d9d9dd")));
            note_pen.setStyle(Qt::DashLine);
            painter.setPen(note_pen);
        }
        painter.drawRoundedRect(rect, 4.0, 4.0);
        if (canonical.muted) {
            painter.drawLine(rect.topLeft() + QPointF(3, 3), rect.bottomRight() - QPointF(3, 3));
        }
        if (selected) {
            painter.fillRect(QRectF(rect.right() - 6.0, rect.top() + 2.0, 3.0, rect.height() - 4.0),
                QColor(QStringLiteral("#ffffff")));
        }
        const auto visible_pitch = editor_.projectedPitch(canonical.pitch);
        if (rect.width() >= 34.0) {
            painter.setPen(QColor(canonical.muted ? QStringLiteral("#333333") : QStringLiteral("#ffffff")));
            painter.drawText(rect.adjusted(6.0, 0.0, -8.0, 0.0),
                Qt::AlignVCenter | Qt::AlignLeft,
                QStringLiteral("%1%2")
                    .arg(in_scale ? QString{} : (editor_.magnetEnabled()
                        ? QStringLiteral("MAG · ") : QStringLiteral("OUT · ")))
                    .arg(QString::fromStdString(core::midiNoteName(visible_pitch))));
        } else if (!in_scale && rect.width() >= 10.0) {
            painter.setPen(QColor(QStringLiteral("#ffffff")));
            painter.drawText(rect, Qt::AlignCenter, QStringLiteral("!"));
        }
    }
    if (marquee_) {
        painter.setPen(QPen(QColor("#507aaf"), 1, Qt::DashLine));
        painter.setBrush(QColor(80, 122, 175, 35));
        painter.drawRect(marquee_->normalized());
    }
    if (const auto x = playheadViewportX()) {
        QPen playhead_pen(QColor(QStringLiteral("#e34848")));
        playhead_pen.setWidth(2);
        painter.setPen(playhead_pen);
        painter.drawLine(QPointF(*x, grid_top), QPointF(*x, viewport()->height()));
    }
    painter.restore();

    painter.fillRect(0, 0, kPitchAxisWidth, viewport()->height(), QColor(QStringLiteral("#ffffff")));
    painter.fillRect(0, 0, viewport()->width(), kTimeAxisHeight, QColor(QStringLiteral("#f5f5f6")));
    painter.setPen(QColor(QStringLiteral("#c9c9cc")));
    painter.drawLine(kPitchAxisWidth, 0, kPitchAxisWidth, viewport()->height());
    painter.drawLine(0, kTimeAxisHeight, viewport()->width(), kTimeAxisHeight);

    painter.setFont(QFont(painter.font().family(), 8));
    for (int pitch = 127; pitch >= 0; --pitch) {
        const auto row_y = grid_top + transform_.pitchToY(static_cast<std::uint8_t>(pitch))
            - verticalScrollBar()->value();
        if (row_y + transform_.pixels_per_semitone < grid_top || row_y > viewport()->height()) {
            continue;
        }
        const auto row = QRectF(0, row_y, kPitchAxisWidth - 1, transform_.pixels_per_semitone);
        const auto in_scale = editor_.pitchIsInScale(static_cast<std::uint8_t>(pitch));
        painter.fillRect(row, isBlackKey(pitch)
            ? (in_scale ? QColor(QStringLiteral("#344338")) : QColor(QStringLiteral("#262626")))
            : (in_scale ? QColor(QStringLiteral("#eaf2eb")) : QColor(QStringLiteral("#ffffff"))));
        painter.setPen(isBlackKey(pitch) ? QColor(QStringLiteral("#ffffff"))
                                         : QColor(QStringLiteral("#242424")));
        painter.drawText(row.adjusted(4.0, 0.0, -5.0, 0.0),
            Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("%1%2")
                .arg(QString::fromStdString(core::midiNoteName(pitch)))
                .arg(in_scale ? QStringLiteral(" ·") : QString{}));
    }

    const auto label_step = transform_.timeToX(0.5) >= 52.0 ? 0.5 : 1.0;
    const auto first_label = std::floor(visible_start / label_step) * label_step;
    painter.setPen(QColor(QStringLiteral("#333333")));
    for (auto time = first_label; time <= visible_end + label_step; time += label_step) {
        const auto x = grid_left + transform_.timeToX(time)
            - horizontalScrollBar()->value();
        painter.drawText(QRectF(x + 3.0, 0.0, 70.0, kTimeAxisHeight),
            Qt::AlignVCenter | Qt::AlignLeft,
            QStringLiteral("%1 s").arg(time, 0, 'f', 1));
    }
    if (const auto x = playheadViewportX()) {
        painter.setBrush(QColor(QStringLiteral("#e34848")));
        painter.setPen(Qt::NoPen);
        const QPolygonF marker{
            QPointF(*x - 5.0, 0.0), QPointF(*x + 5.0, 0.0), QPointF(*x, 8.0)};
        painter.drawPolygon(marker);
    }

    if (const auto range = loop_preview_ ? loop_preview_ : loop_range_) {
        const auto left = kPitchAxisWidth + transform_.timeToX(range->start_seconds) - horizontalScrollBar()->value();
        const auto right = kPitchAxisWidth + transform_.timeToX(range->end_seconds) - horizontalScrollBar()->value();
        painter.save(); painter.setClipRect(QRect(kPitchAxisWidth, 0, viewport()->width() - kPitchAxisWidth, viewport()->height()));
        painter.fillRect(QRectF(left, 0, right - left, viewport()->height()), QColor(69, 136, 186, loop_enabled_ || loop_preview_ ? 42 : 15));
        painter.fillRect(QRectF(left, 0, right - left, kTimeAxisHeight), QColor("#d9eaf7"));
        painter.setPen(QPen(QColor("#327aa8"), 2, loop_enabled_ || loop_preview_ ? Qt::SolidLine : Qt::DashLine));
        painter.drawLine(QPointF(left, 0), QPointF(left, viewport()->height()));
        painter.drawLine(QPointF(right, 0), QPointF(right, viewport()->height()));
        painter.drawText(QPointF(left + 6, 20), QStringLiteral("LOOP %1–%2 s").arg(range->start_seconds,0,'f',2).arg(range->end_seconds,0,'f',2));
        painter.restore();
    }
    if (editor_.notes().empty()) {
        const auto empty = QRect(kPitchAxisWidth + 20, kTimeAxisHeight + 20,
            std::max(0, viewport()->width() - kPitchAxisWidth - 40),
            std::max(0, viewport()->height() - kTimeAxisHeight - 40));
        painter.setPen(QColor(QStringLiteral("#444444")));
        auto font = painter.font();
        font.setPointSize(13);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(empty.adjusted(0, 0, 0, -18), Qt::AlignCenter,
            QStringLiteral("No melody notes yet"));
        font.setPointSize(10);
        font.setBold(false);
        painter.setFont(font);
        painter.setPen(QColor(QStringLiteral("#777777")));
        painter.drawText(empty.adjusted(0, 28, 0, 0), Qt::AlignCenter,
            QStringLiteral("Record a melody and press Stop, or double-click the grid to add a note."));
    }
}

double PianoRollWidget::loopTime(double x) const {
    return std::clamp(rhythm_snap_.apply(transform_.xToTime(contentX(x))), 0.0, 600.0);
}
void PianoRollWidget::setLoopTool(bool enabled) {
    loop_tool_ = enabled; loop_anchor_.reset(); loop_preview_.reset(); viewport()->update();
}
void PianoRollWidget::setLoopRange(std::optional<core::LoopRange> range, bool enabled) {
    loop_range_ = range; loop_enabled_ = enabled; viewport()->update();
}
void PianoRollWidget::setLoopChangedCallback(std::function<void(core::LoopRange)> callback) {
    loop_changed_ = std::move(callback);
}
void PianoRollWidget::mousePressEvent(QMouseEvent* event) {
    if (loop_tool_ && event->button() == Qt::LeftButton && event->position().x() >= kPitchAxisWidth) {
        setFocus(Qt::MouseFocusReason);
        loop_anchor_ = loopTime(event->position().x());
        loop_preview_ = core::LoopRange{*loop_anchor_, *loop_anchor_};
        viewport()->update(); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && event->position().x() >= 0
        && event->position().x() < kPitchAxisWidth && event->position().y() >= kTimeAxisHeight) {
        const auto pitch = transform_.yToPitch(contentY(event->position().y()));
        if (pitch && audition_callback_) { audition_callback_(*pitch); }
        setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton
        || event->position().x() < kPitchAxisWidth
        || event->position().y() < kTimeAxisHeight) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    if (tool_ == Tool::zoom) {
        const auto factor = event->modifiers() & Qt::AltModifier ? 0.8 : 1.25;
        const auto time = transform_.xToTime(contentX(event->position().x()));
        setHorizontalZoom(transform_.pixels_per_second * factor);
        horizontalScrollBar()->setValue(static_cast<int>(transform_.timeToX(time)
            - event->position().x() + kPitchAxisWidth));
        return;
    }
    const auto hit = hitTest(event->position());
    if (!hit) {
        if (tool_ == Tool::paint) { mouseDoubleClickEvent(event); return; }
        if (tool_ != Tool::select) { return; }
        marquee_initial_ = event->modifiers() & Qt::ShiftModifier ? editor_.selectedIndices() : std::vector<std::size_t>{};
        drag_origin_ = event->position();
        marquee_ = QRectF(drag_origin_, drag_origin_);
        static_cast<void>(editor_.selectMany(marquee_initial_));
        notifySelectionChanged();
        viewport()->update();
        return;
    }
    if (tool_ == Tool::select && (event->modifiers() & Qt::ShiftModifier)) {
        auto selected = editor_.selectedIndices();
        if (editor_.isSelected(*hit)) { std::erase(selected, *hit); }
        else { selected.push_back(*hit); }
        static_cast<void>(editor_.selectMany(std::move(selected)));
        notifySelectionChanged(); viewport()->update(); return;
    }
    if (!editor_.isSelected(*hit) || tool_ == Tool::resize) { static_cast<void>(editor_.select(hit)); }
    notifySelectionChanged();
    if (tool_ == Tool::erase) { deleteSelected(); return; }
    if (tool_ == Tool::mute) {
        showEditResult(editor_.toggleMuteSelected(), QStringLiteral("Updated note mute state.")); return;
    }
    if (tool_ == Tool::slice) {
        showEditResult(editor_.splitSelected(rhythm_snap_.apply(transform_.xToTime(contentX(event->position().x())))),
            QStringLiteral("Sliced selected notes.")); return;
    }
    drag_index_ = hit;
    drag_origin_ = event->position();
    drag_original_ = editor_.notes()[*hit];
    drag_preview_ = drag_original_;
    drag_origin_pitch_ = transform_.yToPitch(contentY(event->position().y()));
    drag_display_original_pitch_ = editor_.projectedNotes()[*hit].pitch;
    const auto rect = noteViewportRect(*hit);
    drag_mode_ = tool_ == Tool::resize || std::abs(event->position().x() - rect.right()) <= 9.0
        ? DragMode::resize : DragMode::move;
    if (drag_mode_ == DragMode::resize) { static_cast<void>(editor_.select(hit)); }
    viewport()->setCursor(drag_mode_ == DragMode::resize
        ? Qt::SizeHorCursor : Qt::ClosedHandCursor);
    viewport()->update();
}

void PianoRollWidget::setAuditionCallback(std::function<void(std::uint8_t)> callback) {
    audition_callback_ = std::move(callback);
}

void PianoRollWidget::mouseMoveEvent(QMouseEvent* event) {
    if (loop_anchor_) {
        const auto time = loopTime(event->position().x());
        loop_preview_ = core::LoopRange{std::min(*loop_anchor_, time), std::max(*loop_anchor_, time)};
        viewport()->update(); event->accept(); return;
    }
    if (marquee_ && (event->buttons() & Qt::LeftButton)) {
        marquee_ = QRectF(drag_origin_, event->position()).normalized();
        auto selected = marquee_initial_;
        for (std::size_t index = 0; index < editor_.notes().size(); ++index) {
            if (marquee_->intersects(noteViewportRect(index))) { selected.push_back(index); }
        }
        static_cast<void>(editor_.selectMany(std::move(selected)));
        notifySelectionChanged(); viewport()->update(); return;
    }
    if (drag_mode_ != DragMode::none && (event->buttons() & Qt::LeftButton)) {
        drag_preview_ = previewFor(event->position());
        viewport()->update();
        return;
    }
    const auto hit = hitTest(event->position());
    if (hit) {
        const auto rect = noteViewportRect(*hit);
        viewport()->setCursor(std::abs(event->position().x() - rect.right()) <= 9.0
            ? Qt::SizeHorCursor : Qt::OpenHandCursor);
        QToolTip::showText(event->globalPosition().toPoint(), noteTooltip(*hit), viewport());
    } else {
        viewport()->setCursor(Qt::CrossCursor);
        QToolTip::hideText();
    }
}

void PianoRollWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (loop_anchor_ && event->button() == Qt::LeftButton) {
        const auto time = loopTime(event->position().x());
        const core::LoopRange range{std::min(*loop_anchor_, time), std::max(*loop_anchor_, time)};
        loop_anchor_.reset(); loop_preview_.reset();
        if (range.isValid()) { loop_range_ = range; if (loop_changed_) { loop_changed_(range); } }
        viewport()->update(); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && marquee_) {
        const auto rectangle = QRectF(drag_origin_, event->position()).normalized();
        auto selected = marquee_initial_;
        for (std::size_t index = 0; index < editor_.notes().size(); ++index) {
            if (rectangle.intersects(noteViewportRect(index))) { selected.push_back(index); }
        }
        static_cast<void>(editor_.selectMany(std::move(selected)));
        marquee_.reset(); notifySelectionChanged(); viewport()->update(); return;
    }
    if (event->button() != Qt::LeftButton || drag_mode_ == DragMode::none) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    const auto mode = drag_mode_;
    const auto preview = std::optional<core::TimedMidiNote>{previewFor(event->position())};
    drag_mode_ = DragMode::none;
    drag_preview_.reset();
    drag_origin_pitch_.reset();
    drag_display_original_pitch_.reset();
    viewport()->setCursor(Qt::OpenHandCursor);
    if (preview) {
        showEditResult(
            mode == DragMode::resize ? editor_.updateSelected(*preview, "Resize note")
                : editor_.moveSelected(preview->start_seconds - drag_original_.start_seconds,
                    static_cast<int>(preview->pitch) - static_cast<int>(drag_original_.pitch)),
            mode == DragMode::resize
                ? QStringLiteral("Resized the selected note.")
                : QStringLiteral("Moved the selected note."));
    }
    viewport()->update();
}

void PianoRollWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (loop_tool_) { event->accept(); return; }
    if ((tool_ != Tool::select && tool_ != Tool::paint)
        || event->button() != Qt::LeftButton
        || event->position().x() < kPitchAxisWidth
        || event->position().y() < kTimeAxisHeight
        || hitTest(event->position())) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    const auto pitch = transform_.yToPitch(contentY(event->position().y()));
    if (!pitch) {
        return;
    }
    const auto start = rhythm_snap_.apply(transform_.xToTime(contentX(event->position().x())));
    showEditResult(editor_.add({*pitch, 96, start,
        rhythm_snap_.enabled ? rhythm_snap_.interval_seconds : kDefaultAddedDurationSeconds, 0}),
        QStringLiteral("Added %1 at %2 seconds.")
            .arg(QString::fromStdString(core::midiNoteName(*pitch)))
            .arg(start, 0, 'f', 3));
}

void PianoRollWidget::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::SelectAll)) {
        editor_.selectAll(); notifySelectionChanged(); viewport()->update(); event->accept(); return;
    }
    if (event->key() == Qt::Key_Escape) {
        loop_anchor_.reset(); loop_preview_.reset();
        drag_mode_ = DragMode::none; drag_preview_.reset(); marquee_.reset();
        viewport()->update(); event->accept(); return;
    }
    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected();
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PianoRollWidget::wheelEvent(QWheelEvent* event) {
    const auto factor = std::pow(1.0015, event->angleDelta().y());
    if (event->modifiers() & Qt::ControlModifier) {
        setHorizontalZoom(transform_.pixels_per_second * factor);
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        setVerticalZoom(transform_.pixels_per_semitone * factor);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void PianoRollWidget::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    if (initial_focus_pending_) {
        initial_focus_pending_ = false;
        focusNotes();
    }
}

double PianoRollWidget::contentX(const double viewport_x) const noexcept {
    return viewport_x - kPitchAxisWidth + horizontalScrollBar()->value();
}

double PianoRollWidget::contentY(const double viewport_y) const noexcept {
    return viewport_y - kTimeAxisHeight + verticalScrollBar()->value();
}

std::optional<std::size_t> PianoRollWidget::hitTest(const QPointF& position) const {
    for (std::size_t reverse = editor_.notes().size(); reverse > 0; --reverse) {
        const auto index = reverse - 1;
        if (noteViewportRect(index).adjusted(-2.0, -1.0, 2.0, 1.0).contains(position)) {
            return index;
        }
    }
    return std::nullopt;
}

core::TimedMidiNote PianoRollWidget::previewFor(const QPointF& position) const {
    auto note = drag_original_;
    // Clicking a note (or its resize handle) selects it without changing timing.
    if ((position - drag_origin_).manhattanLength() < QApplication::startDragDistance()) {
        return note;
    }
    if (drag_mode_ == DragMode::resize) {
        auto end = transform_.xToTime(contentX(position.x()));
        end = rhythm_snap_.apply(end);
        note.duration_seconds = std::max(kMinimumUiDurationSeconds, end - note.start_seconds);
        return note;
    }
    const auto delta_seconds = transform_.xToTime(position.x() - drag_origin_.x());
    note.start_seconds = rhythm_snap_.apply(std::max(0.0, drag_original_.start_seconds + delta_seconds));
    const auto current_pitch = transform_.yToPitch(contentY(position.y()));
    if (current_pitch && drag_origin_pitch_ && drag_display_original_pitch_) {
        const auto delta_pitch = static_cast<int>(*current_pitch)
            - static_cast<int>(*drag_origin_pitch_);
        if (delta_pitch != 0) {
            note.pitch = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(*drag_display_original_pitch_) + delta_pitch, 0, 127));
        }
    }
    auto delta_time = note.start_seconds - drag_original_.start_seconds;
    auto delta_pitch = static_cast<int>(note.pitch) - static_cast<int>(drag_original_.pitch);
    double earliest = drag_original_.start_seconds;
    int lowest = 127, highest = 0;
    for (const auto index : editor_.selectedIndices()) {
        const auto& selected = editor_.notes()[index];
        earliest = std::min(earliest, selected.start_seconds);
        lowest = std::min(lowest, static_cast<int>(selected.pitch));
        highest = std::max(highest, static_cast<int>(selected.pitch));
    }
    delta_time = std::max(delta_time, -earliest);
    delta_pitch = std::clamp(delta_pitch, -lowest, 127 - highest);
    note.start_seconds = drag_original_.start_seconds + delta_time;
    note.pitch = static_cast<std::uint8_t>(static_cast<int>(drag_original_.pitch) + delta_pitch);
    return note;
}

QString PianoRollWidget::noteTooltip(const std::size_t index) const {
    const auto guidance = noteGuidanceText(index);
    return guidance.isEmpty() ? QString{} : guidance
        + QStringLiteral("\nDrag to move. Drag the right edge to change length. Press Delete to remove.");
}

QString PianoRollWidget::noteGuidanceText(const std::size_t index) const {
    if (index >= editor_.notes().size()) {
        return {};
    }
    const auto& canonical = editor_.notes()[index];
    const auto& visible = editor_.projectedNotes()[index];
    const auto canonical_name = QString::fromStdString(core::midiNoteName(canonical.pitch));
    const auto visible_name = QString::fromStdString(core::midiNoteName(visible.pitch));
    QString guidance;
    if (editor_.pitchIsInScale(canonical.pitch)) {
        guidance = QStringLiteral("IN SCALE · %1 (MIDI %2)")
            .arg(canonical_name)
            .arg(canonical.pitch);
    } else if (editor_.magnetEnabled()) {
        guidance = QStringLiteral("MAGNET VIEW · original %1 (MIDI %2) → %3 (MIDI %4)")
            .arg(canonical_name)
            .arg(canonical.pitch)
            .arg(visible_name)
            .arg(visible.pitch);
    } else {
        guidance = QStringLiteral("OUT OF SCALE · %1 (MIDI %2) · Magnet is OFF")
            .arg(canonical_name)
            .arg(canonical.pitch);
    }
    return guidance + QStringLiteral("\nStarts at %1 seconds · lasts %2 seconds")
        .arg(canonical.start_seconds, 0, 'f', 3)
        .arg(canonical.duration_seconds, 0, 'f', 3);
}

void PianoRollWidget::updateScrollBars() {
    auto end_seconds = kMinimumTimelineSeconds;
    for (const auto& note : editor_.notes()) {
        end_seconds = std::max(end_seconds,
            note.start_seconds + note.duration_seconds + 2.0);
    }
    const auto content_width = static_cast<int>(std::ceil(transform_.timeToX(end_seconds)));
    const auto page_width = std::max(1, viewport()->width() - kPitchAxisWidth);
    horizontalScrollBar()->setPageStep(page_width);
    horizontalScrollBar()->setRange(0, std::max(0, content_width - page_width));

    const auto content_height = static_cast<int>(std::ceil(transform_.contentHeight()));
    const auto page_height = std::max(1, viewport()->height() - kTimeAxisHeight);
    verticalScrollBar()->setPageStep(page_height);
    verticalScrollBar()->setRange(0, std::max(0, content_height - page_height));
}

void PianoRollWidget::notifyDocumentChanged(const QString& message) {
    updateScrollBars();
    viewport()->update();
    if (document_changed_callback_) {
        document_changed_callback_();
    }
    if (status_callback_) {
        status_callback_(message);
    }
}

void PianoRollWidget::notifySelectionChanged() {
    if (selection_changed_callback_) {
        selection_changed_callback_();
    }
}

void PianoRollWidget::showEditResult(
    const core::NoteEditResult result,
    const QString& success_message) {
    if (result == core::NoteEditResult::applied) {
        notifySelectionChanged();
        notifyDocumentChanged(success_message);
        return;
    }
    if (result == core::NoteEditResult::no_change) {
        viewport()->update();
        return;
    }
    if (status_callback_) {
        status_callback_(QString::fromLatin1(core::noteEditResultText(result)));
    }
}

} // namespace hum::app
