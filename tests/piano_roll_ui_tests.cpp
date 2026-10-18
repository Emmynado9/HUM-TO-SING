#include "PianoRollWidget.hpp"
#include "hum/core/note_editor.hpp"
#include "hum/core/project_file.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void expect(const bool condition, const std::string_view message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] hum::core::TimedMidiNote note(
    const std::uint8_t pitch,
    const double start,
    const double duration) {
    return {pitch, 96, start, duration, 0};
}

void settle() {
    QApplication::processEvents(QEventLoop::AllEvents, 50);
}

void testEmptyAddUndoRedoAndPainting() {
    hum::core::NoteEditor editor;
    hum::app::PianoRollWidget roll(editor);
    roll.resize(900, 420);
    roll.show();
    roll.focusNotes();
    settle();

    QImage image(roll.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        roll.viewport()->render(&painter);
    }
    expect(!image.isNull(), "empty-state piano roll renders offscreen");

    roll.addNoteAtViewportCenter();
    settle();
    expect(editor.notes().size() == 1, "Add note control creates a canonical note");
    expect(editor.selectedIndex() == 0, "new UI note is selected");
    expect(roll.noteViewportRect(0).isValid(), "added note has a valid screen rectangle");
    roll.undo();
    expect(editor.notes().empty(), "UI undo removes the added note");
    roll.redo();
    expect(editor.notes().size() == 1, "UI redo restores the added note");
    roll.deleteSelected();
    expect(editor.notes().empty(), "UI delete removes the selected note");
}

void testSelectionZoomAndGestureCommit() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(60, 0.5, 0.5), note(64, 1.25, 0.4)}),
        "UI fixture initializes");
    hum::app::PianoRollWidget roll(editor);
    roll.resize(900, 420);
    roll.show();
    roll.refresh(true);
    settle();

    const auto initial_rect = roll.noteViewportRect(0);
    expect(initial_rect.intersects(roll.viewport()->rect()),
        "focusNotes scrolls the fixture into view");
    QTest::mouseClick(
        roll.viewport(), Qt::LeftButton, Qt::NoModifier, initial_rect.center().toPoint());
    settle();
    expect(editor.selectedIndex() == 0, "click selects the corresponding canonical note");

    expect(roll.horizontalScrollBar()->maximum() > 0,
        "timeline exposes horizontal scrolling");
    expect(roll.verticalScrollBar()->maximum() > 0,
        "full MIDI pitch range exposes vertical scrolling");
    const auto before_scroll = roll.noteViewportRect(0);
    const auto original_horizontal_scroll = roll.horizontalScrollBar()->value();
    const auto original_vertical_scroll = roll.verticalScrollBar()->value();
    roll.horizontalScrollBar()->setValue(std::min(
        roll.horizontalScrollBar()->maximum(), original_horizontal_scroll + 40));
    roll.verticalScrollBar()->setValue(std::min(
        roll.verticalScrollBar()->maximum(), original_vertical_scroll + 20));
    expect(roll.noteViewportRect(0).topLeft() != before_scroll.topLeft(),
        "scrollbar offsets change note viewport coordinates");
    roll.horizontalScrollBar()->setValue(original_horizontal_scroll);
    roll.verticalScrollBar()->setValue(original_vertical_scroll);

    const auto initial_width = roll.noteViewportRect(0).width();
    roll.setHorizontalZoom(320.0);
    settle();
    expect(roll.noteViewportRect(0).width() > initial_width * 1.9,
        "horizontal zoom changes note width from canonical duration");
    const auto initial_height = roll.noteViewportRect(0).height();
    roll.setVerticalZoom(30.0);
    settle();
    expect(roll.noteViewportRect(0).height() > initial_height,
        "vertical zoom changes semitone row height");

    const auto before_drag = editor.notes()[0];
    const auto drag_rect = roll.noteViewportRect(0);
    const auto drag_start = drag_rect.center().toPoint();
    const auto drag_end = drag_start + QPoint(80, -30);
    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, drag_start);
    QTest::mouseMove(roll.viewport(), drag_end, 10);
    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, drag_end);
    settle();
    expect(editor.notes()[0].start_seconds > before_drag.start_seconds,
        "drag-to-move changes canonical onset");
    expect(editor.notes()[0].pitch == before_drag.pitch + 1,
        "drag-to-move changes canonical pitch by screen row");
    expect(editor.undoLabel() == "Move note", "whole drag creates one move history entry");
    roll.undo();
    expect(std::abs(editor.notes()[0].start_seconds - before_drag.start_seconds) < 1.0e-12,
        "one UI undo restores the pre-drag onset");
    expect(editor.notes()[0].pitch == before_drag.pitch,
        "one UI undo restores the pre-drag pitch");

    const auto resize_rect = roll.noteViewportRect(0);
    const auto resize_start = QPoint(
        static_cast<int>(std::floor(resize_rect.right() - 2.0)),
        static_cast<int>(std::floor(resize_rect.center().y())));
    const auto resize_end = resize_start + QPoint(96, 0);
    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, resize_start);
    QTest::mouseMove(roll.viewport(), resize_end, 10);
    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, resize_end);
    settle();
    expect(editor.notes()[0].duration_seconds > before_drag.duration_seconds,
        "right-edge drag resizes canonical duration");
    expect(editor.undoLabel() == "Resize note", "resize gesture creates one history entry");
}

void testOptionalSnapAndDensePaintResponsiveness() {
    hum::core::NoteEditor editor;
    constexpr std::size_t note_count = 2'000;
    std::vector<hum::core::TimedMidiNote> notes;
    notes.reserve(note_count);
    for (std::size_t index = 0; index < note_count; ++index) {
        notes.push_back(note(
            static_cast<std::uint8_t>(36 + index % 48),
            static_cast<double>(index) * 0.015,
            0.09));
    }
    expect(editor.replaceAll(std::move(notes)), "dense UI fixture initializes");
    hum::app::PianoRollWidget roll(editor);
    roll.resize(1'000, 500);
    roll.show();
    roll.refresh(true);
    roll.setRhythmSnap({true, 0.25});
    expect(roll.rhythmSnap().enabled && roll.rhythmSnap().interval_seconds == 0.25,
        "UI exposes explicit rhythmic timing snap");
    settle();

    QElapsedTimer timer;
    timer.start();
    roll.viewport()->repaint();
    settle();
    const auto elapsed_ms = timer.nsecsElapsed() / 1'000'000.0;
    std::cout << "dense piano roll: painted " << note_count << " notes in "
              << elapsed_ms << " ms (offscreen current build)\n";
    expect(elapsed_ms < 1'000.0,
        "dense offscreen piano-roll paint completes within one second in Debug");
}

void testScaleGuidanceAndMagnetEditing() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(61, 0.5, 0.5)}),
        "scale-guidance UI fixture initializes");
    expect(editor.setScale(hum::core::makeScaleDefinition(
               0, hum::core::ScaleMode::major)),
        "piano roll uses C-major guidance");
    hum::app::PianoRollWidget roll(editor);
    roll.resize(900, 420);
    roll.show();
    roll.refresh(true);
    settle();

    const auto canonical_rect = roll.noteViewportRect(0);
    expect(roll.noteGuidanceText(0).contains(QStringLiteral("OUT OF SCALE")),
        "Magnet-off note has a non-color out-of-scale cue");
    expect(roll.noteGuidanceText(0).contains(QStringLiteral("Magnet is OFF")),
        "Magnet-off guidance explains preservation");

    editor.setMagnetEnabled(true);
    roll.refresh();
    settle();
    const auto snapped_rect = roll.noteViewportRect(0);
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 60,
        "piano roll retains canonical pitch behind snapped view");
    expect(snapped_rect.top() > canonical_rect.top(),
        "C-sharp block visibly moves down to C in the Magnet view");
    expect(roll.noteGuidanceText(0).contains(QStringLiteral("MAGNET VIEW"))
            && roll.noteGuidanceText(0).contains(QStringLiteral("original C#4"))
            && roll.noteGuidanceText(0).contains(QStringLiteral("C4")),
        "Magnet tooltip labels both original and visible pitch");

    expect(editor.setScale(hum::core::makeScaleDefinition(
               2, hum::core::ScaleMode::major)),
        "UI fixture changes to D major");
    roll.refresh();
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 61,
        "key change recomputes visible pitch from canonical note");
    expect(roll.noteGuidanceText(0).contains(QStringLiteral("IN SCALE")),
        "in-scale note has a non-color text cue");
    expect(std::abs(roll.noteViewportRect(0).top() - canonical_rect.top()) < 0.01,
        "D-major key change restores the canonical C-sharp screen row");

    expect(editor.setScale(hum::core::makeScaleDefinition(
               0, hum::core::ScaleMode::major)),
        "UI fixture returns to C major");
    roll.refresh();
    const auto drag_rect = roll.noteViewportRect(0);
    const auto drag_start = drag_rect.center().toPoint();
    const auto drag_end = drag_start + QPoint(0, -40);
    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, drag_start);
    QTest::mouseMove(roll.viewport(), drag_end, 10);
    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, drag_end);
    settle();
    expect(editor.notes()[0].pitch == 62 && editor.projectedNotes()[0].pitch == 62,
        "dragging while Magnet is ON stores the visible target as canonical then reprojects");
    expect(editor.undoLabel() == "Move note",
        "Magnet-on drag remains one transactional edit");
    roll.undo();
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 60,
        "undo restores canonical note and recomputes snapped view");
    roll.redo();
    expect(editor.notes()[0].pitch == 62 && editor.projectedNotes()[0].pitch == 62,
        "redo restores canonical note and matching visible pitch");
}

void testHorizontalMagnetDragPreservesCanonicalPitch() {
    const auto same_note = [](const hum::core::TimedMidiNote& actual,
                              const hum::core::TimedMidiNote& expected) {
        return actual.pitch == expected.pitch && actual.velocity == expected.velocity
            && actual.start_seconds == expected.start_seconds
            && actual.duration_seconds == expected.duration_seconds
            && actual.channel == expected.channel;
    };

    for (const bool snap_enabled : {false, true}) {
        const auto original = note(61, 0.5, 0.5);
        hum::core::NoteEditor editor;
        expect(editor.replaceAll({original}), "horizontal Magnet fixture initializes");
        expect(editor.setScale(hum::core::makeScaleDefinition(
                   0, hum::core::ScaleMode::major)),
            "horizontal Magnet fixture selects C major");
        editor.setMagnetEnabled(true);
        hum::app::PianoRollWidget roll(editor);
        roll.resize(900, 420);
        roll.setHorizontalZoom(160.0);
        roll.setRhythmSnap({snap_enabled, 0.25});
        roll.show();
        roll.refresh(true);
        settle();

        expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 60,
            "horizontal drag begins with canonical C-sharp behind visible C");
        const auto start = roll.noteViewportRect(0).center().toPoint();
        const auto end = start + QPoint(50, 0);
        QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        for (const int offset : {10, 30, 50}) {
            QTest::mouseMove(roll.viewport(), start + QPoint(offset, 0), 10);
        }
        expect(same_note(editor.notes()[0], original) && !editor.canUndo(),
            "intermediate horizontal previews leave canonical data and history unchanged");
        QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, end);
        settle();

        auto moved = original;
        moved.start_seconds = snap_enabled ? 0.75 : 0.8125;
        expect(same_note(editor.notes()[0], moved),
            "horizontal Magnet drag changes only onset, respecting optional rhythm snap");
        auto projected = moved;
        projected.pitch = 60;
        expect(same_note(editor.projectedNotes()[0], projected),
            "horizontal drag keeps visible C and preserves duration, velocity and channel");
        expect(editor.canUndo() && editor.undoLabel() == "Move note" && !editor.canRedo(),
            "horizontal drag commits one move operation on release");

        roll.undo();
        expect(same_note(editor.notes()[0], original),
            "one Undo restores the complete pre-drag canonical note");
        expect(!editor.canUndo() && editor.canRedo(),
            "one Undo exhausts the horizontal gesture history");
        roll.redo();
        expect(same_note(editor.notes()[0], moved) && !editor.canRedo(),
            "one Redo restores only the intended timing change");

        hum::core::ProjectDocument project;
        project.magnet_enabled = true;
        project.rhythm_snap = roll.rhythmSnap();
        project.canonical_notes = editor.notes();
        const auto encoded = hum::core::ProjectFileCodec::encode(project);
        const auto reopened = hum::core::ProjectFileCodec::decode(encoded.value_or(""));
        expect(reopened && reopened.project->magnet_enabled
                && reopened.project->canonical_notes.size() == 1
                && same_note(reopened.project->canonical_notes[0], moved),
            "project round-trip preserves the horizontal edit's canonical C-sharp");

        const auto snapped_midi = hum::core::MidiTimeline::fromTimedNotes(
            editor.projectedNotes(), 120.0, 480);
        const auto expected_start_tick = snap_enabled ? 720U : 780U;
        expect(snapped_midi && snapped_midi->notes.size() == 1
                && snapped_midi->notes[0].pitch == 60
                && snapped_midi->notes[0].start_tick == expected_start_tick
                && snapped_midi->notes[0].duration_ticks == 480
                && snapped_midi->notes[0].velocity == 96
                && snapped_midi->notes[0].channel == original.channel,
            "MIDI projection matches visible C and the edited timing");

        editor.setMagnetEnabled(false);
        roll.refresh();
        expect(same_note(editor.notes()[0], moved)
                && same_note(editor.projectedNotes()[0], moved),
            "Magnet OFF reveals canonical C-sharp with the timing edit intact");
        expect(roll.noteGuidanceText(0).contains(QStringLiteral("C#4")),
            "Magnet OFF labels the restored C-sharp in the piano roll");
        const auto canonical_midi = hum::core::MidiTimeline::fromTimedNotes(
            editor.projectedNotes(), 120.0, 480);
        expect(canonical_midi && canonical_midi->notes.size() == 1
                && canonical_midi->notes[0].pitch == 61
                && canonical_midi->notes[0].start_tick == expected_start_tick
                && canonical_midi->notes[0].duration_ticks == 480
                && canonical_midi->notes[0].velocity == 96
                && canonical_midi->notes[0].channel == original.channel,
            "Magnet OFF exports canonical C-sharp without changing time or expression");
    }
}

void testAudioClockPlayheadProjection() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(60, 0.0, 4.0)}), "playhead UI fixture initializes");
    hum::app::PianoRollWidget roll(editor);
    roll.resize(500, 320);
    roll.show();
    roll.setHorizontalZoom(100.0);
    roll.setPlayheadSeconds(1.25);
    settle();
    const auto first_x = roll.playheadViewportX();
    expect(first_x && std::abs(*first_x - 197.0) < 0.01,
        "audio-clock seconds map to the expected timeline x coordinate");
    roll.setHorizontalZoom(200.0);
    const auto zoomed_x = roll.playheadViewportX();
    expect(zoomed_x && std::abs(*zoomed_x - 322.0) < 0.01,
        "playhead follows time zoom through the shared transform");
    roll.setPlayheadSeconds(3.5, true);
    expect(roll.horizontalScrollBar()->value() > 0,
        "follow mode scrolls a later audio-clock position into view");
    roll.setPlayheadSeconds(std::nullopt);
    expect(!roll.playheadViewportX(), "stopping playback removes the playhead");
}

void testEditingToolsAndGroupGestures() {
    using namespace hum;
    core::NoteEditor editor;
    expect(editor.replaceAll({{61, 96, .5, 1, 0}, {65, 90, 2, 1, 0}}), "tool fixture loads");
    app::PianoRollWidget roll(editor);
    roll.resize(900, 420); roll.show(); roll.focusNotes(); settle();
    auto first = roll.noteViewportRect(0);
    auto second = roll.noteViewportRect(1);
    // Release position must finish the marquee even when there was no intermediate move event.
    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier,
        QPoint(static_cast<int>(first.left()) - 12, static_cast<int>(second.top()) - 8));
    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier,
        QPoint(static_cast<int>(second.right()) + 12, static_cast<int>(first.bottom()) + 8));
    expect(editor.selectedIndices().size() == 2 && !editor.canUndo(), "marquee selects two notes without editing");
    editor.setMagnetEnabled(true); roll.refresh();
    const auto start = roll.noteViewportRect(1).center().toPoint();
    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(80, 0));
    expect(editor.notes()[0].start_seconds == 1 && editor.notes()[1].start_seconds == 2.5
        && editor.notes()[0].pitch == 61 && editor.undoLabel() == "Move notes",
        "dragging a non-primary selection moves the whole group and preserves Magnet originals");
    roll.undo();
    expect(editor.notes()[0].start_seconds == .5 && !editor.canUndo(), "one undo restores group gesture");
    roll.setTool(app::PianoRollWidget::Tool::mute);
    QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier, roll.noteViewportRect(0).center().toPoint());
    expect(editor.notes()[0].muted && editor.notes()[1].muted, "mute tool applies to selected group");
    roll.setTool(app::PianoRollWidget::Tool::slice);
    QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier, roll.noteViewportRect(0).center().toPoint());
    expect(editor.notes().size() == 3 && editor.notes()[2].muted && editor.notes()[2].pitch == 61,
        "slice tool preserves mute and canonical pitch in the new half");
    roll.undo(); roll.undo();
    roll.setTool(app::PianoRollWidget::Tool::select);
    const auto cancel_start = roll.noteViewportRect(0).center().toPoint();
    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, cancel_start);
    QTest::keyClick(&roll, Qt::Key_Escape);
    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, cancel_start + QPoint(100, 0));
    expect(!editor.canUndo() && editor.notes()[0].start_seconds == .5, "Escape cancels a drag without committing");
    roll.setTool(app::PianoRollWidget::Tool::paint);
    roll.setRhythmSnap({true, .25});
    const auto empty = roll.noteViewportRect(1).center().toPoint() + QPoint(240, 0);
    QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier, empty);
    expect(editor.notes().size() == 3 && editor.notes().back().duration_seconds == .25,
        "paint tool creates one grid-length note");
    roll.setTool(app::PianoRollWidget::Tool::resize);
    const auto resize_start = roll.noteViewportRect(2).center().toPoint();
    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, resize_start);
    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, resize_start + QPoint(80, 0));
    expect(editor.notes()[2].duration_seconds > .25, "resize tool changes duration from the note body");
    roll.setTool(app::PianoRollWidget::Tool::erase);
    QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier, roll.noteViewportRect(2).center().toPoint());
    expect(editor.notes().size() == 2, "erase tool removes the clicked note");
    roll.setTool(app::PianoRollWidget::Tool::zoom);
    const auto zoom = roll.transform().pixels_per_second;
    QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier, empty);
    expect(roll.transform().pixels_per_second > zoom && editor.notes().size() == 2,
        "zoom tool changes view without adding notes");
}

void testSelectionDoesNotEditTiming() {
    using namespace hum;
    for (const bool snap : {false, true}) {
        for (const auto tool : {app::PianoRollWidget::Tool::select, app::PianoRollWidget::Tool::resize}) {
            core::NoteEditor editor;
            const auto original = note(61, .53, .61);
            expect(editor.replaceAll({original}), "selection timing fixture loads");
            app::PianoRollWidget roll(editor);
            roll.resize(900, 420); roll.show(); roll.refresh(true); settle();
            roll.setRhythmSnap({snap, .25});
            roll.setTool(tool);
            int changes = 0;
            roll.setDocumentChangedCallback([&] { ++changes; });
            for (const bool edge : {false, true}) {
                const auto rect = roll.noteViewportRect(0);
                const auto start = edge ? QPoint(qRound(rect.right()) - 4, qRound(rect.center().y()))
                                        : rect.center().toPoint();
                for (const int jitter : {0, 1}) {
                    QTest::mousePress(roll.viewport(), Qt::LeftButton, Qt::NoModifier, start);
                    QTest::mouseRelease(roll.viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(jitter, 0));
                    expect(editor.notes()[0].start_seconds == original.start_seconds
                        && editor.notes()[0].duration_seconds == original.duration_seconds
                        && editor.notes()[0].pitch == original.pitch && !editor.canUndo() && changes == 0,
                        "click or tiny pointer movement selects without snapping, resizing, dirtying or undo history");
                    expect(editor.selectedIndex() == 0, "timing-preserving click still selects the note");
                    if (editor.canUndo()) { roll.undo(); changes = 0; }
                }
            }
        }
    }
}

void testFocusSelectedNote() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(40, 0, .5), note(96, 90, .5)}),
        "distant region focus fixture loads");
    hum::app::PianoRollWidget roll(editor);
    roll.resize(900, 420); roll.show(); settle();
    expect(editor.select(1), "later region selects its canonical note");
    roll.refresh(true);
    expect(roll.viewport()->rect().contains(roll.noteViewportRect(1).toAlignedRect()),
        "opening a distant selected note brings its time and pitch into view");
    expect(editor.notes().size() == 2 && editor.notes()[1].start_seconds == 90 && !editor.canUndo(),
        "focusing a selected note preserves the entire canonical arrangement");
}

} // namespace

void testPianoKeyAudition() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(60,0,.5), note(61,1,.5)}), "key audition fixture loads");
    hum::app::PianoRollWidget roll(editor);
    roll.resize(900,420); roll.show(); roll.refresh(true); settle();
    std::vector<std::uint8_t> heard;
    roll.setAuditionCallback([&](std::uint8_t pitch) { heard.push_back(pitch); });
    for (std::size_t i = 0; i < 2; ++i) {
        QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier,
            QPoint(20, static_cast<int>(roll.noteViewportRect(i).center().y())));
    }
    expect(heard == std::vector<std::uint8_t>{60,61}, "white and black piano keys audition exact pitches");
    expect(editor.notes().size() == 2 && !editor.canUndo(), "piano audition does not edit canonical notes");
    QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20,5));
    expect(heard.size() == 2, "ruler corner does not audition a key");
    roll.setVerticalZoom(30);
    roll.refresh(true); settle();
    QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier,
        QPoint(20, static_cast<int>(roll.noteViewportRect(0).center().y())));
    expect(heard.back() == 60, "piano audition follows pitch zoom and scroll");
}


void testTrackIsolationAndSeeking() {
    using namespace hum::core;
    NoteEditor editor;
    expect(editor.replaceAll({{60,96,.5,.5,0},{64,96,1.5,.5,0},{67,96,2.5,.5,0}},
        {{{0},0,"A"},{{1},1,"B"},{{2},2,"C"}},3), "isolated editor fixture loads");
    hum::app::PianoRollWidget roll(editor);
    roll.resize(950,500);roll.show();roll.setActiveTrack(1);roll.refresh(true);settle();
    roll.setShowOtherTracks(true);
    expect(roll.showOtherTracks(), "other-track reference notes can be shown");
    QTest::keyClick(&roll, Qt::Key_A, Qt::ControlModifier);
    expect(editor.selectedIndices()==std::vector<std::size_t>{1}, "select all includes only active-track notes");
    roll.setTool(hum::app::PianoRollWidget::Tool::erase);
    QTest::mouseClick(roll.viewport(),Qt::LeftButton,Qt::NoModifier,roll.noteViewportRect(0).center().toPoint());
    expect(editor.notes().size()==3 && !editor.canUndo(), "grey reference cannot be erased");
    QTest::mouseClick(roll.viewport(),Qt::LeftButton,Qt::NoModifier,roll.noteViewportRect(2).center().toPoint());
    expect(editor.notes().size()==3, "other later track cannot be edited");
    roll.setTool(hum::app::PianoRollWidget::Tool::select);
    std::optional<double> seek;
    roll.setSeekCallback([&](double seconds){seek=seconds;});
    const auto rect=roll.noteViewportRect(1);
    QTest::mouseClick(roll.viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.left(),8));
    expect(seek && std::abs(*seek-1.5)<.01 && !editor.canUndo(), "piano ruler seeks without editing");
    QTest::mouseClick(roll.viewport(),Qt::LeftButton,Qt::NoModifier,rect.center().toPoint());
    expect(seek && std::abs(*seek-1.75)<.01 && !editor.canUndo(), "note click seeks without changing timing");
    roll.setActiveTrack(0);
    expect(editor.selectedIndices().empty(), "switching tracks drops foreign selection");
    QTest::keyClick(&roll, Qt::Key_A, Qt::ControlModifier);
    roll.deleteSelected();
    expect(editor.notes().size()==2 && editor.notes()[0].pitch==64, "delete affects active track only");
    roll.setActiveTrack(1);roll.undo();
    expect(editor.notes().size()==3 && editor.selectedIndices().empty(), "undo cannot restore editable selection on a foreign track");
    expect(editor.deleteTrack(2)==NoteEditResult::applied, "prepare empty target track");
    expect(editor.addTrack()==NoteEditResult::applied, "new empty lane exists");
    roll.setActiveTrack(2);roll.addNoteAtViewportCenter();
    expect(editor.trackForNote(editor.notes().size()-1)==2, "new piano notes belong to the selected empty track");
    roll.setShowOtherTracks(false);
    expect(!roll.showOtherTracks() && !roll.viewport()->grab().isNull(), "references can be hidden without editing notes");
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    testTrackIsolationAndSeeking();
    testPianoKeyAudition();
    testSelectionDoesNotEditTiming();
    testFocusSelectedNote();
    testEditingToolsAndGroupGestures();
    testEmptyAddUndoRedoAndPainting();
    testSelectionZoomAndGestureCommit();
    testOptionalSnapAndDensePaintResponsiveness();
    testScaleGuidanceAndMagnetEditing();
    testHorizontalMagnetDragPreservesCanonicalPitch();
    testAudioClockPlayheadProjection();

    if (failures == 0) {
        std::cout << "All piano roll UI tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " piano roll UI test assertion(s) failed.\n";
    return 1;
}
