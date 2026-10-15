#include "hum/core/midi_file.hpp"
#include "hum/core/note_editor.hpp"
#include "hum/core/scale.hpp"

#include <algorithm>
#include <array>
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

void testEveryRootAndSupportedMode() {
    constexpr std::array<std::uint8_t, 7> major{0, 2, 4, 5, 7, 9, 11};
    constexpr std::array<std::uint8_t, 7> natural_minor{0, 2, 3, 5, 7, 8, 10};
    for (const auto mode : {hum::core::ScaleMode::major,
             hum::core::ScaleMode::natural_minor}) {
        const auto& expected_intervals = mode == hum::core::ScaleMode::major
            ? major : natural_minor;
        for (std::uint8_t root = 0; root < 12; ++root) {
            const auto scale = hum::core::makeScaleDefinition(root, mode);
            expect(scale.isValid(), "every root produces a valid supported scale");
            expect(scale.root_pitch_class == root, "scale factory retains the selected root");
            expect(std::equal(scale.intervals.cbegin(), scale.intervals.cend(),
                       expected_intervals.cbegin(), expected_intervals.cend()),
                "scale factory uses the documented mode intervals");
            for (int pitch = 0; pitch <= 127; ++pitch) {
                const auto midi_pitch = static_cast<std::uint8_t>(pitch);
                const auto interval = static_cast<std::uint8_t>(
                    (pitch % 12 + 12 - root) % 12);
                const auto expected = std::find(
                    expected_intervals.cbegin(), expected_intervals.cend(), interval)
                    != expected_intervals.cend();
                expect(scale.contains(midi_pitch) == expected,
                    "scale membership matches its interval pattern across MIDI range");
                const auto snapped = scale.nearestPitch(midi_pitch);
                expect(scale.contains(snapped),
                    "every MIDI pitch maps to an in-scale pitch");
                expect(scale.nearestPitch(snapped) == snapped,
                    "applying the same scale mapping twice is idempotent");
            }
        }
    }
    for (std::uint8_t root = 0; root < 12; ++root) {
        expect(hum::core::pitchClassName(root) != "?",
            "all twelve root selectors have a display name");
    }
    expect(hum::core::scaleModeName(hum::core::ScaleMode::major) == "Major",
        "major mode has a plain-language name");
    expect(hum::core::scaleModeName(hum::core::ScaleMode::natural_minor) == "Natural minor",
        "natural minor mode has a plain-language name");
}

void testTieBreaksAndMidiEdges() {
    const auto c_major = hum::core::makeScaleDefinition(0, hum::core::ScaleMode::major);
    expect(c_major.nearestPitch(60) == 60, "already-valid pitch remains unchanged");
    expect(c_major.nearestPitch(61) == 60,
        "equal-distance C sharp tie resolves to the lower scale pitch");
    expect(c_major.nearestPitch(63) == 62,
        "equal-distance D sharp tie resolves to the lower scale pitch");
    expect(c_major.nearestPitch(66) == 65,
        "equal-distance F sharp tie resolves to the lower scale pitch");
    expect(c_major.nearestPitch(0) == 0, "MIDI zero remains valid in C major");
    expect(c_major.nearestPitch(127) == 127, "MIDI 127 remains valid in C major");

    const auto d_major = hum::core::makeScaleDefinition(2, hum::core::ScaleMode::major);
    expect(d_major.nearestPitch(0) == 1,
        "lower MIDI boundary chooses the valid upper pitch when no lower pitch exists");
    const auto c_sharp_major = hum::core::makeScaleDefinition(1, hum::core::ScaleMode::major);
    expect(c_sharp_major.nearestPitch(127) == 126,
        "upper MIDI boundary chooses the valid lower pitch when no upper pitch exists");

    const auto a_minor = hum::core::makeScaleDefinition(9, hum::core::ScaleMode::natural_minor);
    expect(a_minor.contains(57) && a_minor.contains(60) && a_minor.contains(64),
        "A natural minor includes A, C, and E across its octave");
    expect(a_minor.nearestPitch(66) == 65,
        "natural minor uses the same deterministic lower tie break");
}

void testProcessorChangesPitchOnly() {
    const std::vector canonical{
        note(61, 0.125, 0.375, 44),
        note(64, 1.250, 0.625, 111),
    };
    const auto scale = hum::core::makeScaleDefinition(0, hum::core::ScaleMode::major);
    const hum::core::NearestScaleProcessor processor;
    const auto projected = processor.process(canonical, scale);
    expect(projected.size() == canonical.size(), "scale projection preserves note count");
    expect(projected[0].pitch == 60 && projected[1].pitch == 64,
        "scale projection changes only pitches that need guidance");
    for (std::size_t index = 0; index < canonical.size(); ++index) {
        expectNear(projected[index].start_seconds, canonical[index].start_seconds, 0.0,
            "scale projection preserves start time exactly");
        expectNear(projected[index].duration_seconds, canonical[index].duration_seconds, 0.0,
            "scale projection preserves duration exactly");
        expect(projected[index].velocity == canonical[index].velocity,
            "scale projection preserves velocity exactly");
        expect(projected[index].channel == canonical[index].channel,
            "scale projection preserves channel exactly");
    }
}

void testReversibleEditorProjectionAndHistory() {
    hum::core::NoteEditor editor;
    const std::vector canonical{
        note(61, 0.125, 0.375, 44),
        note(64, 1.250, 0.625, 111),
    };
    expect(editor.replaceAll(canonical), "canonical phrase loads into editor");
    expect(editor.setScale(hum::core::makeScaleDefinition(
               0, hum::core::ScaleMode::major)),
        "C major guidance is accepted");
    expect(editor.projectedNotes()[0].pitch == 61,
        "Magnet OFF preserves an out-of-scale canonical pitch");
    expect(!editor.canUndo(),
        "selecting a scale does not create a destructive note-history entry");

    editor.setMagnetEnabled(true);
    expect(editor.notes()[0].pitch == 61,
        "Magnet ON does not replace the canonical pitch");
    expect(editor.projectedNotes()[0].pitch == 60,
        "Magnet ON derives the closest C-major pitch");
    expect(editor.projectedNotes()[1].pitch == 64,
        "Magnet ON leaves an in-scale note unchanged");
    expect(!editor.canUndo(),
        "toggling Magnet does not create a destructive note-history entry");

    editor.setMagnetEnabled(true);
    expect(editor.projectedNotes()[0].pitch == 60,
        "turning Magnet ON twice does not compound snapping");
    expect(editor.setScale(hum::core::makeScaleDefinition(
               2, hum::core::ScaleMode::major)),
        "changing to D major is accepted");
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 61,
        "key change recomputes from canonical C sharp instead of prior snapped C");
    expect(!editor.canUndo(),
        "changing key does not create a note-history entry");

    expect(editor.select(0), "out-of-scale note selects while Magnet is ON");
    auto edited = *editor.selectedNote();
    edited.pitch = 63;
    edited.start_seconds = 0.500;
    expect(editor.updateSelected(edited, "Move note") == hum::core::NoteEditResult::applied,
        "editing while Magnet is ON changes the canonical note transactionally");
    expect(editor.notes()[0].pitch == 63,
        "Magnet-on edit stores the manually chosen canonical pitch");
    expect(editor.projectedNotes()[0].pitch == 62,
        "Magnet-on edit immediately derives from the new canonical pitch");
    expect(editor.undo(), "Magnet-on canonical edit can be undone");
    expect(editor.notes()[0].pitch == 61 && editor.projectedNotes()[0].pitch == 61,
        "undo restores canonical pitch and recomputes current-key projection");
    expect(editor.redo(), "Magnet-on canonical edit can be redone");
    expect(editor.notes()[0].pitch == 63 && editor.projectedNotes()[0].pitch == 62,
        "redo restores canonical edit and its current-key projection");

    editor.setMagnetEnabled(false);
    expect(editor.projectedNotes()[0].pitch == 63,
        "Magnet ON to OFF reveals the canonical edited pitch");
    expectNear(editor.projectedNotes()[0].start_seconds, 0.500, 0.0,
        "Magnet toggle preserves edited onset");
    expectNear(editor.projectedNotes()[0].duration_seconds, canonical[0].duration_seconds, 0.0,
        "Magnet toggle preserves duration");
    expect(editor.projectedNotes()[0].velocity == canonical[0].velocity,
        "Magnet toggle preserves velocity");

    const auto retained_root = editor.scale().root_pitch_class;
    expect(!editor.setScale({12, {0, 2, 4}}), "invalid scale selection is rejected");
    expect(editor.scale().root_pitch_class == retained_root,
        "invalid scale selection does not replace current guidance");
}

void testVisibleAndExportedPitchConsistency() {
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({note(61, 0.5, 0.25)}), "export fixture loads");
    expect(editor.setScale(hum::core::makeScaleDefinition(
               0, hum::core::ScaleMode::major)),
        "export fixture selects C major");
    editor.setMagnetEnabled(true);
    expect(editor.projectedNotes()[0].pitch == 60,
        "visible projection exposes the snapped pitch");

    const auto sequence = hum::core::MidiTimeline::fromTimedNotes(
        editor.projectedNotes(), 120.0, 480);
    expect(sequence && sequence->notes[0].pitch == editor.projectedNotes()[0].pitch,
        "MIDI timeline pitch matches the visible Magnet pitch");
    const auto bytes = sequence
        ? hum::core::StandardMidiFileExporter::serialize(*sequence) : std::nullopt;
    const std::array<std::uint8_t, 3> expected_note_on{0x90U, 60U, 96U};
    expect(bytes && std::search(bytes->cbegin(), bytes->cend(),
                        expected_note_on.cbegin(), expected_note_on.cend()) != bytes->cend(),
        "serialized MIDI contains the same snapped pitch shown by the editor");

    editor.setMagnetEnabled(false);
    const auto canonical_sequence = hum::core::MidiTimeline::fromTimedNotes(
        editor.projectedNotes(), 120.0, 480);
    expect(canonical_sequence && canonical_sequence->notes[0].pitch == 61,
        "Magnet OFF restores canonical pitch for both view and export");
    expect(sequence && canonical_sequence
            && sequence->notes[0].start_tick == canonical_sequence->notes[0].start_tick,
        "Magnet does not alter exported onset ticks");
    expect(sequence && canonical_sequence
            && sequence->notes[0].duration_ticks == canonical_sequence->notes[0].duration_ticks,
        "Magnet does not alter exported duration ticks");
    expect(sequence && canonical_sequence
            && sequence->notes[0].velocity == canonical_sequence->notes[0].velocity,
        "Magnet does not alter exported velocity");
}

} // namespace

int main() {
    testEveryRootAndSupportedMode();
    testTieBreaksAndMidiEdges();
    testProcessorChangesPitchOnly();
    testReversibleEditorProjectionAndHistory();
    testVisibleAndExportedPitchConsistency();

    if (failures == 0) {
        std::cout << "All scale guidance tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " scale guidance test assertion(s) failed.\n";
    return 1;
}
