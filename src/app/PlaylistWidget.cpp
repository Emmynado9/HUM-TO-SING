#include "PlaylistWidget.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hum::app {
namespace {
constexpr int kTrackWidth = 190, kRulerHeight = 34, kLaneHeight = 112, kFooterHeight = 30;
}
PlaylistWidget::PlaylistWidget(core::NoteEditor& editor, QWidget* parent)
    : QAbstractScrollArea(parent), editor_(editor) {
    setAccessibleName(QStringLiteral("Melody playlist"));
    setAccessibleDescription(QStringLiteral("Arrange phrase regions on track lanes. Drag to move, Ctrl+D to duplicate, Delete to remove, Enter to edit notes."));
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setToolTip(QStringLiteral("Drag a region left or right to arrange timing, or up and down to change track. Double-click to edit notes. All lanes use the built-in soft tone."));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); });
    refresh();
}
void PlaylistWidget::refresh() {
    if (selected_region_ && *selected_region_ >= editor_.regions().size()) { selected_region_.reset(); }
    selected_track_ = std::min(selected_track_, editor_.trackCount() - 1);
    updateScrollRange(); viewport()->update();
}
void PlaylistWidget::setTempo(double bpm) {
    if (std::isfinite(bpm) && bpm >= 20 && bpm <= 400 && tempo_bpm_ != bpm) {
        tempo_bpm_ = bpm; refresh();
    }
}
void PlaylistWidget::setHorizontalZoom(double value) {
    if (std::isfinite(value)) { pixels_per_second_ = std::clamp(value, 50.0, 520.0); refresh(); }
}
void PlaylistWidget::setRhythmSnap(core::RhythmSnap snap) { if (snap.isValid()) { rhythm_snap_ = snap; } }
void PlaylistWidget::setPlayheadSeconds(std::optional<double> seconds, bool follow) {
    playhead_seconds_ = seconds && std::isfinite(*seconds) && *seconds >= 0 ? seconds : std::nullopt;
    if (follow && playhead_seconds_) {
        const auto x = timeToX(*playhead_seconds_);
        if (x < kTrackWidth || x > viewport()->width() - 24) {
            horizontalScrollBar()->setValue(static_cast<int>(std::clamp(*playhead_seconds_ * pixels_per_second_,
                0.0, static_cast<double>(horizontalScrollBar()->maximum()))));
        }
    }
    viewport()->update();
}
void PlaylistWidget::setOpenEditorCallback(std::function<void()> callback) { open_editor_ = std::move(callback); }
void PlaylistWidget::setDocumentChangedCallback(std::function<void()> callback) { document_changed_ = std::move(callback); }
void PlaylistWidget::setSelectionChangedCallback(std::function<void()> callback) { selection_changed_ = std::move(callback); }
std::optional<std::size_t> PlaylistWidget::selectedRegion() const {
    return selected_region_ && *selected_region_ < editor_.regions().size() ? selected_region_ : std::nullopt;
}
std::size_t PlaylistWidget::selectedTrack() const { return std::min(selected_track_, editor_.trackCount() - 1); }
void PlaylistWidget::changed(core::NoteEditResult result) {
    dragging_ = false; refresh();
    if (result == core::NoteEditResult::applied && document_changed_) { document_changed_(); }
    if (selection_changed_) { selection_changed_(); }
}
void PlaylistWidget::duplicateSelected() {
    if (const auto index = selectedRegion()) {
        const auto result = editor_.duplicateRegion(*index);
        if (result == core::NoteEditResult::applied) { selected_region_ = editor_.regions().size() - 1; }
        changed(result);
    }
}
void PlaylistWidget::deleteSelected() {
    if (const auto index = selectedRegion()) {
        const auto result = editor_.deleteRegion(*index); selected_region_.reset(); changed(result);
    }
}
double PlaylistWidget::timeToX(double seconds) const { return kTrackWidth + seconds * pixels_per_second_ - horizontalScrollBar()->value(); }
std::size_t PlaylistWidget::trackAt(double y) const {
    const auto lane = std::max(0.0, std::floor((y + verticalScrollBar()->value() - kRulerHeight) / kLaneHeight));
    return static_cast<std::size_t>(std::min(lane, static_cast<double>(editor_.trackCount() - 1)));
}
QRectF PlaylistWidget::regionViewportRect(std::size_t index) const {
    if (index >= editor_.regions().size()) { return {}; }
    const auto& region = editor_.regions()[index];
    double start = std::numeric_limits<double>::max(), end = 0;
    for (const auto i : region.note_indices) {
        const auto& note = editor_.notes()[i];
        start = std::min(start, note.start_seconds);
        end = std::max(end, note.start_seconds + note.duration_seconds);
    }
    const auto preview = dragging_ && selected_region_ == index;
    const auto track = preview ? drag_track_ : region.track;
    return {timeToX(start + (preview ? drag_delta_seconds_ : 0)),
        kRulerHeight + track * kLaneHeight + 12.0 - verticalScrollBar()->value(),
        std::max(4.0, (end - start) * pixels_per_second_), kLaneHeight - 24.0};
}
std::optional<double> PlaylistWidget::playheadViewportX() const {
    return playhead_seconds_ ? std::optional<double>{timeToX(*playhead_seconds_)} : std::nullopt;
}
void PlaylistWidget::updateScrollRange() {
    double end = 0;
    for (const auto& note : editor_.notes()) { end = std::max(end, note.start_seconds + note.duration_seconds); }
    const auto bar = 240.0 / tempo_bpm_;
    const auto duration = std::max(4 * bar, (std::ceil(end / bar) + 1) * bar);
    const auto available = std::max(0, viewport()->width() - kTrackWidth);
    horizontalScrollBar()->setPageStep(available);
    horizontalScrollBar()->setRange(0, static_cast<int>(std::clamp(duration * pixels_per_second_ - available,
        0.0, static_cast<double>(std::numeric_limits<int>::max()))));
    const auto height = std::max(0, viewport()->height() - kRulerHeight - kFooterHeight);
    verticalScrollBar()->setPageStep(height);
    verticalScrollBar()->setRange(0, std::max(0, static_cast<int>(editor_.trackCount()) * kLaneHeight - height));
}
void PlaylistWidget::paintEvent(QPaintEvent*) {
    QPainter p(viewport()); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(viewport()->rect(), QColor("#fafafa"));
    const auto body = QRect(0, kRulerHeight, viewport()->width(), std::max(0, viewport()->height() - kRulerHeight - kFooterHeight));
    p.save(); p.setClipRect(body);
    auto font = p.font(); font.setPixelSize(11); p.setFont(font);
    for (std::size_t track = 0; track < editor_.trackCount(); ++track) {
        const auto y = kRulerHeight + static_cast<int>(track) * kLaneHeight - verticalScrollBar()->value();
        p.fillRect(0, y, kTrackWidth, kLaneHeight, track == selectedTrack() ? QColor("#eef3ef") : Qt::white);
        p.fillRect(kTrackWidth, y, viewport()->width() - kTrackWidth, kLaneHeight, QColor(track % 2 ? "#f7f8f7" : "#f2f6f2"));
        p.fillRect(0, y, 4, kLaneHeight, QColor("#4f6255"));
        p.setPen(QColor("#999999")); p.drawText(QPoint(17, y + 31), QStringLiteral("%1").arg(track + 1, 2, 10, QLatin1Char('0')));
        font.setPixelSize(15); font.setBold(true); p.setFont(font); p.setPen(QColor("#080808"));
        p.drawText(QPoint(47, y + 31), QStringLiteral("Track %1").arg(track + 1));
        font.setPixelSize(11); font.setBold(false); p.setFont(font); p.setPen(QColor("#777777"));
        p.drawText(QPoint(47, y + 54), QStringLiteral("Soft tone · MIDI"));
        const auto count = std::count_if(editor_.regions().begin(), editor_.regions().end(), [track](const auto& r) { return r.track == track; });
        p.drawText(QPoint(47, y + 82), QStringLiteral("%1 phrase region(s)").arg(count));
        p.setPen(QColor("#dedee1")); p.drawLine(0, y + kLaneHeight, viewport()->width(), y + kLaneHeight);
    }
    p.restore();
    p.save(); p.setClipRect(QRect(kTrackWidth, 0, viewport()->width() - kTrackWidth, viewport()->height() - kFooterHeight));
    p.fillRect(kTrackWidth, 0, viewport()->width(), kRulerHeight, QColor("#f3f3f4"));
    const auto beat_seconds = 60 / tempo_bpm_, beat_pixels = beat_seconds * pixels_per_second_;
    const auto first = static_cast<long long>(std::floor(horizontalScrollBar()->value() / beat_pixels));
    const auto count = static_cast<long long>(std::ceil(viewport()->width() / beat_pixels)) + 2;
    for (auto beat = first; beat <= first + count; ++beat) {
        const auto x = timeToX(beat * beat_seconds); const auto bar = beat % 4 == 0;
        p.setPen(QColor(bar ? "#c9ceca" : "#e2e6e2"));
        p.drawLine(QPointF(x, bar ? 0 : kRulerHeight), QPointF(x, viewport()->height() - kFooterHeight));
        if (bar) { p.setPen(QColor("#444444")); p.drawText(QPointF(x + 8, 22), QString::number(beat / 4 + 1)); }
    }
    p.save(); p.setClipRect(body, Qt::IntersectClip);
    for (std::size_t i = 0; i < editor_.regions().size(); ++i) {
        const auto rect = regionViewportRect(i);
        if (rect.right() < kTrackWidth || rect.left() > viewport()->width() || !rect.intersects(body)) { continue; }
        p.setPen(QPen(QColor(selected_region_ == i ? "#31483a" : "#849b89"), selected_region_ == i ? 2 : 1));
        p.setBrush(QColor("#dfe8e0")); p.drawRoundedRect(rect, 7, 7);
        p.save(); p.setClipRect(rect.adjusted(1, 1, -1, -1), Qt::IntersectClip);
        p.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 25), QColor("#4f6255"));
        font.setBold(true); p.setFont(font); p.setPen(Qt::white);
        p.drawText(QRectF(rect.left() + 10, rect.top(), rect.width() - 18, 25), Qt::AlignLeft | Qt::AlignVCenter,
            QString::fromStdString(editor_.regions()[i].name));
        int low = 127, high = 0;
        for (const auto index : editor_.regions()[i].note_indices) {
            low = std::min(low, static_cast<int>(editor_.projectedNotes()[index].pitch));
            high = std::max(high, static_cast<int>(editor_.projectedNotes()[index].pitch));
        }
        const auto preview = rect.adjusted(0, 34, 0, -12); const auto span = std::max(12, high - low + 1);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#4f6255"));
        for (const auto index : editor_.regions()[i].note_indices) {
            const auto& note = editor_.projectedNotes()[index];
            const auto delta = dragging_ && selected_region_ == i ? drag_delta_seconds_ : 0;
            const auto y = preview.center().y() - (note.pitch - (low + high) / 2.0) * preview.height() / span;
            p.drawRoundedRect(QRectF(timeToX(note.start_seconds + delta), y,
                std::max(2.0, note.duration_seconds * pixels_per_second_), 3), 1.5, 1.5);
        }
        p.restore();
    }
    if (editor_.regions().empty()) {
        p.setPen(QColor("#4f6255")); font.setPixelSize(16); font.setBold(true); p.setFont(font);
        p.drawText(QPoint(kTrackWidth + 24, kRulerHeight + 42), QStringLiteral("Your next melody starts here"));
        font.setPixelSize(12); font.setBold(false); p.setFont(font); p.setPen(QColor("#777777"));
        p.drawText(QPoint(kTrackWidth + 24, kRulerHeight + 68), QStringLiteral("Open Hum → MIDI to record, or Piano roll to add notes."));
    }
    p.restore();
    if (const auto x = playheadViewportX()) {
        p.setPen(QPen(QColor("#e34848"), 2)); p.drawLine(QPointF(*x, 0), QPointF(*x, viewport()->height() - kFooterHeight));
        p.setBrush(QColor("#e34848")); p.drawPolygon(QPolygonF{QPointF(*x - 5, 0), QPointF(*x + 5, 0), QPointF(*x, 8)});
    }
    p.restore();
    p.fillRect(0, 0, kTrackWidth, kRulerHeight, QColor("#f3f3f4"));
    font.setPixelSize(10); font.setBold(true); p.setFont(font); p.setPen(QColor("#777777"));
    p.drawText(QRect(16, 0, kTrackWidth - 24, kRulerHeight), Qt::AlignVCenter,
        QStringLiteral("TRACKS     4/4 · %1 BPM").arg(tempo_bpm_, 0, 'f', 0));
    p.setPen(QColor("#dedee1")); p.drawLine(kTrackWidth - 1, 0, kTrackWidth - 1, viewport()->height());
    p.fillRect(0, viewport()->height() - kFooterHeight, viewport()->width(), kFooterHeight, Qt::white);
    p.setPen(QColor("#888888")); font.setBold(false); p.setFont(font);
    p.drawText(QRect(16, viewport()->height() - kFooterHeight, viewport()->width() - 32, kFooterHeight), Qt::AlignVCenter,
        QStringLiteral("Drag to arrange · Double-click to edit · Ctrl+D duplicate · Delete remove · Ctrl+scroll zoom · Shift+scroll time"));
}
void PlaylistWidget::resizeEvent(QResizeEvent* event) { QAbstractScrollArea::resizeEvent(event); updateScrollRange(); }
void PlaylistWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || event->position().y() < kRulerHeight
        || event->position().y() >= viewport()->height() - kFooterHeight) { return; }
    selected_track_ = trackAt(event->position().y()); selected_region_.reset(); dragging_ = false;
    if (event->position().x() >= kTrackWidth) {
        for (std::size_t i = editor_.regions().size(); i-- > 0;) {
            if (regionViewportRect(i).contains(event->position())) { selected_region_ = i; break; }
        }
    }
    if (selected_region_) {
        dragging_ = true; drag_origin_ = event->position(); drag_delta_seconds_ = 0; drag_track_ = selected_track_;
    }
    if (selection_changed_) { selection_changed_(); } viewport()->update(); event->accept();
}
void PlaylistWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_ || !selectedRegion() || !(event->buttons() & Qt::LeftButton)) { return; }
    double start = std::numeric_limits<double>::max();
    for (const auto i : editor_.regions()[*selected_region_].note_indices) { start = std::min(start, editor_.notes()[i].start_seconds); }
    const auto target = rhythm_snap_.apply(start + (event->position().x() - drag_origin_.x()) / pixels_per_second_);
    drag_delta_seconds_ = target - start; drag_track_ = trackAt(event->position().y()); viewport()->update(); event->accept();
}
void PlaylistWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragging_ && selectedRegion()) {
        double start = std::numeric_limits<double>::max();
        for (const auto i : editor_.regions()[*selected_region_].note_indices) {
            start = std::min(start, editor_.notes()[i].start_seconds);
        }
        drag_delta_seconds_ = rhythm_snap_.apply(start
            + (event->position().x() - drag_origin_.x()) / pixels_per_second_) - start;
        drag_track_ = trackAt(event->position().y());
        const auto result = editor_.moveRegion(*selected_region_, drag_delta_seconds_, drag_track_);
        selected_track_ = drag_track_; changed(result); event->accept();
    }
}
void PlaylistWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().x() >= kTrackWidth) {
        for (std::size_t i = editor_.regions().size(); i-- > 0;) {
            if (regionViewportRect(i).contains(event->position())) {
                dragging_ = false; selected_region_ = i;
                static_cast<void>(editor_.select(editor_.regions()[i].note_indices.front()));
                if (open_editor_) { open_editor_(); } event->accept(); return;
            }
        }
    }
}
void PlaylistWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && dragging_) { dragging_ = false; refresh(); event->accept(); return; }
    if (event->matches(QKeySequence::Undo)) { if (editor_.undo()) { changed(core::NoteEditResult::applied); } }
    else if (event->matches(QKeySequence::Redo)) { if (editor_.redo()) { changed(core::NoteEditResult::applied); } }
    else if (event->key() == Qt::Key_D && event->modifiers() & Qt::ControlModifier) { duplicateSelected(); }
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) { deleteSelected(); }
    else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && open_editor_) { open_editor_(); }
    else { QAbstractScrollArea::keyPressEvent(event); return; }
    event->accept();
}
void PlaylistWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) { setHorizontalZoom(pixels_per_second_ * std::pow(1.0015, event->angleDelta().y())); }
    else if ((event->modifiers() & Qt::ShiftModifier) || event->angleDelta().x() != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (event->angleDelta().x() ? event->angleDelta().x() : event->angleDelta().y()));
    } else { verticalScrollBar()->setValue(verticalScrollBar()->value() - event->angleDelta().y()); }
    event->accept();
}
} // namespace hum::app
