#include "PlaylistWidget.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTest>

#include <cmath>
#include <iostream>
#include <string_view>

namespace {
int failures = 0;
int checks = 0;
void expect(const bool condition, const std::string_view message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
bool near(const double actual, const double expected) {
    return std::abs(actual - expected) < 0.01;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    hum::core::NoteEditor editor;
    hum::app::PlaylistWidget playlist(editor);
    playlist.resize(900, 280);
    playlist.setHorizontalZoom(100.0);
    playlist.show();
    QApplication::processEvents();
    double sought = -1;
    playlist.setSeekCallback([&](double seconds) { sought = seconds; });
    QTest::mouseClick(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(390, 10));
    expect(near(sought, 2.0) && playlist.playheadViewportX() && near(*playlist.playheadViewportX(), 390),
        "ruler click seeks at the displayed time without editing notes");
    expect(!editor.canUndo(), "ruler seeking creates no note edit");
    expect(playlist.regionViewportRect().isEmpty(), "empty playlist invents no region");
    expect(!playlist.viewport()->grab().isNull(), "empty playlist paints offscreen");

    expect(editor.replaceAll({{61, 96, 0.5, 0.5, 0}, {67, 90, 3.5, 0.5, 0}}),
        "fixture loads into canonical editor");
    playlist.refresh();
    const auto region = playlist.regionViewportRect();
    expect(near(region.left(), 240.0) && near(region.width(), 350.0),
        "region preserves leading silence, internal rest and phrase duration");
    playlist.setTempo(96.0);
    expect(playlist.regionViewportRect() == region,
        "bar-guide tempo change does not retime notes or region");
    expect(editor.setScale(hum::core::makeScaleDefinition(0, hum::core::ScaleMode::major)),
        "fixture selects C major");
    editor.setMagnetEnabled(true);
    playlist.refresh();
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 60,
        "playlist reads Magnet projection without replacing canonical pitch");
    expect(playlist.regionViewportRect() == region,
        "scale projection preserves region timing");

    expect(editor.select(0), "fixture selects the first note");
    expect(editor.moveSelected(0.25, 0) == hum::core::NoteEditResult::applied,
        "canonical editor changes onset");
    playlist.refresh();
    expect(near(playlist.regionViewportRect().left(), 265.0),
        "playlist reflects canonical edits without a second model");
    expect(editor.undo(), "canonical editor undoes the onset change");
    playlist.refresh();
    expect(playlist.regionViewportRect() == region, "playlist reflects undo");

    playlist.setHorizontalZoom(200.0);
    expect(near(playlist.regionViewportRect().width(), 700.0), "time zoom scales region width");
    playlist.setHorizontalZoom(100.0);
    playlist.horizontalScrollBar()->setValue(50);
    expect(near(playlist.regionViewportRect().left(), 190.0), "horizontal scrolling shifts the region");
    playlist.setPlayheadSeconds(2.0);
    expect(playlist.playheadViewportX() && near(*playlist.playheadViewportX(), 340.0),
        "audio-clock playhead shares the timeline scale and scroll offset");
    playlist.setPlayheadSeconds(8.0, true);
    expect(playlist.playheadViewportX()
            && *playlist.playheadViewportX() < playlist.viewport()->width(),
        "follow mode brings a later playhead into view");
    playlist.setPlayheadSeconds(std::nullopt);
    expect(!playlist.playheadViewportX(), "Stop clears the playlist playhead");

    playlist.horizontalScrollBar()->setValue(0);
    int editor_requests = 0;
    playlist.setOpenEditorCallback([&] { ++editor_requests; });
    QTest::mouseDClick(playlist.viewport(), Qt::LeftButton, Qt::NoModifier,
        playlist.regionViewportRect().center().toPoint());
    expect(editor_requests == 1, "double-click opens the existing piano-roll editor");
    QTest::keyClick(&playlist, Qt::Key_Return);
    expect(editor_requests == 2, "keyboard Enter opens the editor");
    expect(editor.notes().size() == 2 && editor.notes()[0].pitch == 61
            && editor.notes()[0].start_seconds == 0.5,
        "playlist navigation preserves the canonical phrase");
    expect(!playlist.viewport()->grab().isNull(), "populated playlist paints offscreen");

    expect(editor.replaceAll({{61, 96, 0.5, 0.5, 2}, {64, 72, 1.25, 0.25, 2}}),
        "arrangement gesture fixture loads");
    expect(editor.addTrack() == hum::core::NoteEditResult::applied, "second lane adds");
    editor.clearHistory();
    playlist.refresh();
    playlist.horizontalScrollBar()->setValue(0);
    playlist.verticalScrollBar()->setValue(0);
    int document_changes = 0;
    playlist.setDocumentChangedCallback([&] { ++document_changes; });
    const auto drag_start = playlist.regionViewportRect().center().toPoint();
    const auto drag_end = drag_start + QPoint(50, 112);
    QTest::mousePress(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, drag_start);
    QMouseEvent move(QEvent::MouseMove, QPointF(drag_end), QPointF(drag_end),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(playlist.viewport(), &move);
    expect(editor.notes()[0].start_seconds == 0.5 && !editor.canUndo(),
        "region drag preview leaves canonical data and history untouched");
    QTest::mouseRelease(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, drag_end);
    expect(editor.notes()[0].start_seconds == 1.0 && editor.regions()[0].track == 1,
        "region drag commits timing and destination lane");
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 60
            && editor.notes()[0].duration_seconds == 0.5
            && editor.notes()[0].velocity == 96 && editor.notes()[0].channel == 2,
        "region drag under Magnet preserves canonical pitch and every other note field");
    expect(document_changes == 1 && editor.undoLabel() == "Move region",
        "one drag publishes exactly one transaction");
    QTest::keyClick(&playlist, Qt::Key_Z, Qt::ControlModifier);
    expect(editor.notes()[0].start_seconds == 0.5 && editor.regions()[0].track == 0
            && !editor.canUndo(), "one keyboard undo restores timing and lane");
    QTest::keyClick(&playlist, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    expect(editor.notes()[0].start_seconds == 1.0 && editor.regions()[0].track == 1,
        "keyboard redo restores the arranged region");
    QTest::keyClick(&playlist, Qt::Key_D, Qt::ControlModifier);
    expect(editor.regions().size() == 2 && editor.notes().size() == 4,
        "duplicate creates real canonical events, not a decorative clip");
    QTest::keyClick(&playlist, Qt::Key_Delete);
    expect(editor.regions().size() == 1 && editor.notes().size() == 2,
        "Delete removes the selected region and its notes");
    QTest::keyClick(&playlist, Qt::Key_Z, Qt::ControlModifier);
    expect(editor.regions().size() == 2 && editor.notes().size() == 4,
        "region deletion undoes without losing its notes");
    playlist.setRhythmSnap({true, 0.25});
    playlist.verticalScrollBar()->setValue(0);
    const auto snap_start = playlist.regionViewportRect(0).center().toPoint();
    const auto snap_end = snap_start + QPoint(23, 0);
    QTest::mousePress(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, snap_start);
    QTest::mouseRelease(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, snap_end);
    expect(editor.notes()[0].start_seconds == 1.25,
        "explicit rhythm snap affects region placement only when enabled");
    playlist.setRhythmSnap({false, 0.25});
    const auto escape_start = playlist.regionViewportRect(0).center().toPoint();
    QTest::mousePress(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, escape_start);
    QTest::keyClick(&playlist, Qt::Key_Escape);
    QTest::mouseRelease(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, escape_start + QPoint(30, 0));
    expect(editor.notes()[0].start_seconds == 1.25, "Escape cancels a region drag");

    expect(editor.replaceAll({{61, 96, .53, .61, 0}, {67, 90, 2.13, .4, 0}},
        {{{0}, 0, "First"}, {{1}, 1, "Second"}}, 2), "off-grid selection fixture loads");
    playlist.refresh();
    playlist.setRhythmSnap({true, .25});
    playlist.horizontalScrollBar()->setValue(0);
    playlist.verticalScrollBar()->setValue(0);
    document_changes = 0;
    const auto original_notes = editor.notes();
    const auto timing_unchanged = [&] {
        return editor.notes()[0].start_seconds == original_notes[0].start_seconds
            && editor.notes()[1].start_seconds == original_notes[1].start_seconds
            && editor.notes()[0].duration_seconds == original_notes[0].duration_seconds
            && editor.notes()[1].duration_seconds == original_notes[1].duration_seconds;
    };
    const auto click_start = playlist.regionViewportRect(0).center().toPoint();
    for (const int jitter : {0, 1}) {
        QTest::mousePress(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, click_start);
        QTest::mouseRelease(playlist.viewport(), Qt::LeftButton, Qt::NoModifier, click_start + QPoint(jitter, 0));
        expect(timing_unchanged() && !editor.canUndo() && document_changes == 0,
            "selecting an off-grid region with Snap preserves notes and history even with small pointer jitter");
        expect(playlist.selectedRegion() == 0, "timing-preserving click selects the region");
        if (editor.canUndo()) { static_cast<void>(editor.undo()); playlist.refresh(); document_changes = 0; }
    }
    expect(editor.select(0), "keyboard navigation starts with a note from another region");
    QTest::mouseClick(playlist.viewport(), Qt::LeftButton, Qt::NoModifier,
        playlist.regionViewportRect(1).center().toPoint());
    editor_requests = 0;
    bool selected_before_open = false;
    playlist.setOpenEditorCallback([&] { ++editor_requests; selected_before_open = editor.selectedIndex() == 1; });
    QTest::keyClick(&playlist, Qt::Key_Return);
    expect(editor_requests == 1 && selected_before_open && playlist.selectedTrack() == 1,
        "Enter selects the chosen region's canonical note before opening the editor");
    expect(timing_unchanged() && !editor.canUndo(),
        "opening an off-grid region leaves canonical timing and history untouched");

    if (failures) {
        std::cerr << failures << " playlist assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All playlist UI tests passed (" << checks << " assertions).\n";
    return 0;
}
