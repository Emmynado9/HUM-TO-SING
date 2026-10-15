#pragma once

#include "hum/core/note_editor.hpp"

#include <QAbstractScrollArea>
#include <QRectF>

#include <cstddef>
#include <functional>
#include <optional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace hum::app {

class PianoRollWidget final : public QAbstractScrollArea {
public:
    explicit PianoRollWidget(core::NoteEditor& editor, QWidget* parent = nullptr);

    void setDocumentChangedCallback(std::function<void()> callback);
    void setSelectionChangedCallback(std::function<void()> callback);
    void setStatusCallback(std::function<void(const QString&)> callback);

    void setHorizontalZoom(double pixels_per_second);
    void setVerticalZoom(double pixels_per_semitone);
    void setRhythmSnap(core::RhythmSnap snap);
    void setPlayheadSeconds(std::optional<double> seconds, bool follow = false);
    [[nodiscard]] core::PianoRollTransform transform() const noexcept;
    [[nodiscard]] core::RhythmSnap rhythmSnap() const noexcept;
    [[nodiscard]] std::optional<double> playheadViewportX() const noexcept;

    void refresh(bool focus_notes = false);
    void focusNotes();
    void addNoteAtViewportCenter();
    void deleteSelected();
    void undo();
    void redo();

    [[nodiscard]] QRectF noteViewportRect(std::size_t index) const;
    [[nodiscard]] QString noteGuidanceText(std::size_t index) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class DragMode {
        none,
        move,
        resize,
    };

    [[nodiscard]] double contentX(double viewport_x) const noexcept;
    [[nodiscard]] double contentY(double viewport_y) const noexcept;
    [[nodiscard]] std::optional<std::size_t> hitTest(const QPointF& position) const;
    [[nodiscard]] core::TimedMidiNote previewFor(const QPointF& position) const;
    [[nodiscard]] QString noteTooltip(std::size_t index) const;
    void updateScrollBars();
    void notifyDocumentChanged(const QString& message);
    void notifySelectionChanged();
    void showEditResult(core::NoteEditResult result, const QString& success_message);

    core::NoteEditor& editor_;
    core::PianoRollTransform transform_;
    core::RhythmSnap rhythm_snap_;
    std::function<void()> document_changed_callback_;
    std::function<void()> selection_changed_callback_;
    std::function<void(const QString&)> status_callback_;
    DragMode drag_mode_{DragMode::none};
    QPointF drag_origin_;
    core::TimedMidiNote drag_original_;
    std::optional<core::TimedMidiNote> drag_preview_;
    std::optional<std::uint8_t> drag_origin_pitch_;
    std::optional<std::uint8_t> drag_display_original_pitch_;
    std::optional<double> playhead_seconds_;
    bool initial_focus_pending_{true};
};

} // namespace hum::app
