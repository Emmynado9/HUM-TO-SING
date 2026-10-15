#pragma once
#include "hum/core/note_editor.hpp"
#include <QAbstractScrollArea>
#include <QRectF>
#include <functional>
#include <optional>

namespace hum::app {
// Arrangement view and gestures; all note/region transactions live in NoteEditor.
class PlaylistWidget final : public QAbstractScrollArea {
public:
    explicit PlaylistWidget(core::NoteEditor& editor, QWidget* parent = nullptr);
    void refresh();
    void setTempo(double bpm);
    void setHorizontalZoom(double pixels_per_second);
    void setRhythmSnap(core::RhythmSnap snap);
    void setPlayheadSeconds(std::optional<double> seconds, bool follow = false);
    void setOpenEditorCallback(std::function<void()> callback);
    void setDocumentChangedCallback(std::function<void()> callback);
    void setSelectionChangedCallback(std::function<void()> callback);
    void duplicateSelected();
    void deleteSelected();
    [[nodiscard]] std::optional<std::size_t> selectedRegion() const;
    [[nodiscard]] std::size_t selectedTrack() const;
    [[nodiscard]] QRectF regionViewportRect(std::size_t index = 0) const;
    [[nodiscard]] std::optional<double> playheadViewportX() const;
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    [[nodiscard]] double timeToX(double seconds) const;
    [[nodiscard]] std::size_t trackAt(double y) const;
    void updateScrollRange();
    void changed(core::NoteEditResult result);
    core::NoteEditor& editor_;
    double tempo_bpm_{120.0}, pixels_per_second_{160.0};
    core::RhythmSnap rhythm_snap_{};
    std::optional<double> playhead_seconds_;
    std::optional<std::size_t> selected_region_;
    std::size_t selected_track_{0};
    bool dragging_{false};
    QPointF drag_origin_;
    double drag_delta_seconds_{0};
    std::size_t drag_track_{0};
    std::function<void()> open_editor_, document_changed_, selection_changed_;
};
} // namespace hum::app
