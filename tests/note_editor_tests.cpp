#include "hum/core/midi_file.hpp"
#include "hum/core/note_editor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
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

void expectNear(
    const double actual,
    const double expected,
    const double tolerance,
    const std::string_view message) {
    expect(std::abs(actual - expected) <= tolerance, message);
}

[[nodiscard]] hum::core::TimedMidiNote note(
    const std::uint8_t pitch,
    const double start,
    const double duration,
    const std::uint8_t velocity = 96) {
    return {pitch, velocity, start, duration, 0};
}

void testCoordinateRoundTrips() {
    for (const auto zoom : std::vector<hum::core::PianoRollTransform>{
             {50.0, 10.0}, {160.0, 20.0}, {520.0, 34.0}}) {
        expect(zoom.isValid(), "supported piano-roll zoom is valid");
        for (const auto seconds : {0.0, 0.125, 1.75, 12.5}) {
            expectNear(zoom.xToTime(zoom.timeToX(seconds)), seconds, 1.0e-12,
                "time-to-screen conversion round-trips at each zoom");
        }
        for (const auto pitch : {0U, 1U, 36U, 60U, 69U, 126U, 127U}) {
            const auto y = zoom.pitchToY(static_cast<std::uint8_t>(pitch))
                + zoom.pixels_per_semitone / 2.0;
            expect(zoom.yToPitch(y) == pitch,
                "pitch-to-screen conversion round-trips at each zoom");
        }
        expect(!zoom.yToPitch(-0.1), "negative screen pitch coordinate is rejected");
        expect(!zoom.yToPitch(zoom.contentHeight()),
            "coordinate below the MIDI pitch range is rejected");
    }
}

void testSelectionAddDeleteAndHistory() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(60, 0.25, 0.5), note(64, 1.0, 0.25)}),
        "valid transcription initializes the editor");
    expect(editor.notes().size() == 2, "initial notes use the canonical timed MIDI model");
    expect(!editor.canUndo() && !editor.canRedo(), "loading transcription resets edit history");
    expect(!editor.select(9), "out-of-range selection is rejected");
    expect(editor.select(1), "existing note can be selected");
    expect(editor.selectedNote() && editor.selectedNote()->pitch == 64,
        "selection exposes the canonical note");

    expect(editor.add(note(67, 1.5, 0.4)) == hum::core::NoteEditResult::applied,
        "add creates one edit command");
    expect(editor.notes().size() == 3 && editor.selectedIndex() == 2,
        "added note is canonical and selected");
    expect(editor.undoLabel() == "Add note", "undo describes the add operation");
    expect(editor.undo(), "add can be undone");
    expect(editor.notes().size() == 2 && editor.selectedIndex() == 1,
        "undo restores notes and prior selection");
    expect(editor.redo(), "add can be redone");
    expect(editor.notes().size() == 3 && editor.selectedIndex() == 2,
        "redo restores the added note and selection");

    expect(editor.deleteSelected() == hum::core::NoteEditResult::applied,
        "selected note can be deleted");
    expect(editor.notes().size() == 2, "delete removes exactly one note");
    expect(editor.undo(), "delete can be undone");
    expect(editor.notes().size() == 3, "undo restores the deleted note");

    expect(editor.moveSelected(0.25, 1) == hum::core::NoteEditResult::applied,
        "new edit after undo succeeds");
    expect(!editor.canRedo(), "new edit after undo discards the redo branch");
}

void testMoveResizeBoundariesAndSingleGestureHistory() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(60, 0.5, 0.5)}), "single-note editor initializes");
    expect(editor.select(0), "single note selects");
    const auto original = *editor.selectedNote();

    auto dragged = original;
    dragged.pitch = 62;
    dragged.start_seconds = 1.25;
    expect(editor.updateSelected(dragged, "Move note") == hum::core::NoteEditResult::applied,
        "completed drag applies one transactional move");
    expect(editor.undoLabel() == "Move note", "drag creates one logical history label");
    expect(editor.undo(), "one undo reverses the entire drag");
    expectNear(editor.selectedNote()->start_seconds, original.start_seconds, 1.0e-12,
        "drag undo restores the original onset");
    expect(editor.selectedNote()->pitch == original.pitch,
        "drag undo restores the original pitch");
    expect(!editor.canUndo(), "one drag did not create intermediate history entries");
    expect(editor.redo(), "drag redo reapplies the final gesture");

    expect(editor.resizeSelected(0.875) == hum::core::NoteEditResult::applied,
        "positive resize succeeds");
    expectNear(editor.selectedNote()->duration_seconds, 0.875, 1.0e-12,
        "resize updates canonical duration");
    expect(editor.resizeSelected(0.0) == hum::core::NoteEditResult::invalid_note,
        "zero duration is rejected");
    expect(editor.resizeSelected(-0.1) == hum::core::NoteEditResult::invalid_note,
        "negative duration is rejected");
    expect(editor.moveSelected(-10.0, 0) == hum::core::NoteEditResult::invalid_note,
        "negative onset is rejected");
    expect(editor.moveSelected(0.0, 200) == hum::core::NoteEditResult::invalid_note,
        "pitch above MIDI 127 is rejected");

    hum::core::NoteEditor low_editor;
    expect(low_editor.replaceAll({note(0, 0.0, 0.25)}), "lowest MIDI note initializes");
    expect(low_editor.select(0), "lowest note selects");
    expect(low_editor.moveSelected(0.0, -1) == hum::core::NoteEditResult::invalid_note,
        "pitch below MIDI zero is rejected");
}

void testRhythmSnapIsOptional() {
    const hum::core::RhythmSnap off{false, 0.25};
    const hum::core::RhythmSnap on{true, 0.25};
    expectNear(off.apply(0.37), 0.37, 1.0e-12,
        "disabled rhythmic snap preserves captured timing");
    expectNear(on.apply(0.37), 0.25, 1.0e-12,
        "enabled rhythmic snap rounds to the chosen time grid");
    expectNear(on.apply(0.39), 0.5, 1.0e-12,
        "rhythmic snap chooses the nearest grid line");
    expectNear(on.apply(-0.4), 0.0, 1.0e-12,
        "rhythmic snap cannot create negative time");
}

void testOverlapPolicyAndEditedMidiExport() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({
        note(60, 0.0, 1.0),
        note(64, 0.5, 1.0),
        note(60, 1.0, 0.5),
    }), "overlapping fixture initializes");
    expect(editor.overlapPolicy()
            == hum::core::NoteOverlapPolicy::preserve_independent_events,
        "overlap policy explicitly preserves independent events");
    expect(editor.overlapCount() == 2,
        "strict interval overlap excludes notes that only touch at a boundary");
    expect(hum::core::notesOverlap(editor.notes()[0], editor.notes()[1]),
        "notes at different pitches may overlap without deletion");
    expect(!hum::core::notesOverlap(editor.notes()[0], editor.notes()[2]),
        "touching note boundaries do not count as overlap");

    expect(editor.select(1), "overlapping note selects");
    auto changed = *editor.selectedNote();
    changed.pitch = 65;
    changed.start_seconds = 0.75;
    changed.duration_seconds = 0.25;
    expect(editor.updateSelected(changed, "Move note") == hum::core::NoteEditResult::applied,
        "overlapping event can be corrected without deleting peers");
    expect(editor.notes().size() == 3, "editing an overlap preserves every event");

    const auto sequence = hum::core::MidiTimeline::fromTimedNotes(editor.notes(), 120.0, 480);
    expect(sequence.has_value(), "edited canonical notes convert to MIDI");
    expect(sequence && sequence->notes.size() == editor.notes().size(),
        "MIDI export includes every edited and overlapping event");
    const auto edited = sequence ? std::find_if(
        sequence->notes.cbegin(), sequence->notes.cend(), [](const auto& midi_note) {
            return midi_note.pitch == 65;
        }) : std::vector<hum::core::MidiNote>::const_iterator{};
    expect(sequence && edited != sequence->notes.cend(),
        "exported MIDI contains the corrected pitch");
    expect(sequence && edited != sequence->notes.cend() && edited->start_tick == 720,
        "exported MIDI contains the corrected onset");
    expect(sequence && edited != sequence->notes.cend() && edited->duration_ticks == 240,
        "exported MIDI contains the corrected duration");

    const auto bytes = sequence
        ? hum::core::StandardMidiFileExporter::serialize(*sequence) : std::nullopt;
    expect(bytes.has_value(), "edited sequence serializes as a MIDI file");
    const std::array<std::uint8_t, 3> expected_note_on{0x90U, 65U, 96U};
    expect(bytes && std::search(
        bytes->cbegin(), bytes->cend(),
        expected_note_on.cbegin(), expected_note_on.cend()) != bytes->cend(),
        "serialized MIDI contains the edited note-on event");
}

void testDenseDocumentResponsiveness() {
    constexpr std::size_t note_count = 5'000;
    std::vector<hum::core::TimedMidiNote> notes;
    notes.reserve(note_count);
    for (std::size_t index = 0; index < note_count; ++index) {
        notes.push_back(note(
            static_cast<std::uint8_t>(36 + index % 48),
            static_cast<double>(index) * 0.0125,
            0.10));
    }
    hum::core::NoteEditor editor;
    const auto started = std::chrono::steady_clock::now();
    const auto loaded = editor.replaceAll(std::move(notes));
    static_cast<void>(editor.select(note_count / 2));
    const auto moved = editor.moveSelected(0.125, 1);
    const auto overlaps = editor.overlapCount();
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "dense editor: " << note_count << " notes loaded and one transactional move in "
              << elapsed << " ms\n";
    expect(loaded, "dense fixture loads into the canonical editor");
    expect(moved == hum::core::NoteEditResult::applied,
        "dense fixture remains editable");
    expect(editor.notes().size() == note_count, "dense edit preserves note count");
    expect(overlaps > 0, "dense overlap status is calculated without changing notes");
    expect(elapsed < 1'000.0, "dense core edit completes within one second in Debug");
}

void testGroupEditsMuteAndSlice() {
    using namespace hum::core;
    NoteEditor editor;
    expect(editor.replaceAll({{61, 96, .25, 1, 2}, {67, 80, .5, 1, 3}, {72, 90, 2, .5, 0}},
        {{{0, 2}, 0, "First"}, {{1}, 1, "Second"}}, 2), "multi-region fixture loads");
    expect(editor.selectMany({1, 0, 1}) && editor.selectedIndices() == std::vector<std::size_t>({0, 1}),
        "multi-selection sorts and deduplicates");
    expect(!editor.selectMany({99}) && editor.selectedIndices().size() == 2,
        "invalid selection preserves the prior selection");
    editor.setMagnetEnabled(true);
    expect(editor.moveSelected(-1, 0) == NoteEditResult::invalid_note && editor.notes()[0].start_seconds == .25
        && editor.notes()[1].start_seconds == .5 && !editor.canUndo(), "invalid group move is atomic");
    expect(editor.moveSelected(.25, 0) == NoteEditResult::applied && editor.undoLabel() == "Move notes",
        "group move is one history operation");
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 60,
        "group time move retains canonical Magnet pitches");
    expect(editor.undo() && !editor.canUndo() && editor.selectedIndices().size() == 2,
        "group undo restores notes and selection together");
    expect(editor.toggleMuteSelected() == NoteEditResult::applied && editor.notes()[0].muted
        && editor.notes()[1].muted && !editor.notes()[2].muted, "mute affects only selected notes");
    const auto midi = MidiTimeline::fromTimedNotes(editor.projectedNotes());
    expect(midi && midi->notes.size() == 1 && midi->notes.front().pitch == 72,
        "muted projected notes are absent from MIDI export");
    expect(editor.splitSelected(.75) == NoteEditResult::applied && editor.notes().size() == 5,
        "slice splits all selected notes crossing the time");
    expect(editor.notes()[3].muted && editor.notes()[3].channel == 2 && editor.notes()[3].pitch == 61
        && editor.notes()[3].start_seconds == .75 && editor.notes()[3].duration_seconds == .5,
        "slice preserves canonical pitch, channel, mute state and original end");
    expect(editor.regions()[0].note_indices == std::vector<std::size_t>({0, 2, 3})
        && editor.regions()[1].note_indices == std::vector<std::size_t>({1, 4}),
        "slice keeps both halves in their original regions");
    expect(editor.undo() && editor.notes().size() == 3 && editor.notes()[0].duration_seconds == 1,
        "slice undoes in one step");
    expect(editor.deleteSelected() == NoteEditResult::applied && editor.notes().size() == 1
        && editor.regions().size() == 1 && editor.regions()[0].note_indices == std::vector<std::size_t>({0}),
        "group deletion remaps membership and removes empty regions");
    expect(editor.undo() && editor.selectedIndices() == std::vector<std::size_t>({0, 1}),
        "undo deletion restores the full selection");
    expect(editor.deleteRegion(0) == NoteEditResult::applied && editor.notes().size() == 1,
        "region deletion after multi-selection remains valid");
    for (const auto index : editor.selectedIndices()) {
        expect(index < editor.notes().size(), "region deletion leaves no stale selected indices");
    }
}

} // namespace

int main() {
    testGroupEditsMuteAndSlice();
    testCoordinateRoundTrips();
    testSelectionAddDeleteAndHistory();
    testMoveResizeBoundariesAndSingleGestureHistory();
    testRhythmSnapIsOptional();
    testOverlapPolicyAndEditedMidiExport();
    testDenseDocumentResponsiveness();

    if (failures == 0) {
        std::cout << "All note editor tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " note editor test assertion(s) failed.\n";
    return 1;
}
