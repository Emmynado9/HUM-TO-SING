#pragma once

#include "hum/core/midi.hpp"
#include "hum/core/scale.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hum::core {

enum class NoteEditResult {
    applied,
    no_change,
    no_selection,
    invalid_index,
    invalid_note,
};

enum class NoteOverlapPolicy {
    preserve_independent_events,
};

enum class TrackKind : std::uint8_t { melody, chords };
enum class InstrumentVoice : std::uint8_t {
    grand_piano, electric_piano, acoustic_guitar, electric_guitar, strings,
};

struct TrackDefinition {
    TrackKind kind{TrackKind::melody};
    InstrumentVoice instrument{InstrumentVoice::grand_piano};
    bool operator==(const TrackDefinition&) const = default;
};

[[nodiscard]] bool validTrackDefinition(const TrackDefinition& track) noexcept;
[[nodiscard]] const char* trackKindName(TrackKind kind) noexcept;
[[nodiscard]] const char* instrumentVoiceName(InstrumentVoice instrument) noexcept;
[[nodiscard]] std::uint8_t instrumentVoiceMidiProgram(InstrumentVoice instrument) noexcept;

struct RhythmSnap {
    bool enabled{false};
    double interval_seconds{0.125};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] double apply(double seconds) const noexcept;
};

struct PianoRollTransform {
    double pixels_per_second{160.0};
    double pixels_per_semitone{20.0};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] double timeToX(double seconds) const noexcept;
    [[nodiscard]] double xToTime(double x) const noexcept;
    [[nodiscard]] double pitchToY(std::uint8_t pitch) const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> yToPitch(double y) const noexcept;
    [[nodiscard]] double contentHeight() const noexcept;
};

// Groups refer to the one canonical note vector; lanes are arrangement labels,
// not independent copies of MIDI events or instrument routing.
struct PlaylistRegion {
    std::vector<std::size_t> note_indices;
    std::size_t track{0};
    std::string name{"Melody"};
    bool operator==(const PlaylistRegion&) const = default;
};

[[nodiscard]] bool validPlaylistRegions(const std::vector<PlaylistRegion>& regions,
    std::size_t note_count, std::size_t track_count) noexcept;

class NoteEditor final {
public:
    [[nodiscard]] bool replaceAll(std::vector<TimedMidiNote> notes,
        std::vector<PlaylistRegion> regions = {}, std::size_t track_count = 1,
        std::vector<TrackDefinition> tracks = {});
    [[nodiscard]] const std::vector<PlaylistRegion>& regions() const noexcept;
    [[nodiscard]] std::size_t trackCount() const noexcept;
    [[nodiscard]] const std::vector<TrackDefinition>& tracks() const noexcept;
    [[nodiscard]] NoteEditResult addTrack(TrackDefinition track = {});
    [[nodiscard]] NoteEditResult setTrackInstrument(std::size_t track, InstrumentVoice instrument);
    [[nodiscard]] NoteEditResult appendRegion(std::vector<TimedMidiNote> notes,
        double offset_seconds, std::size_t track);
    [[nodiscard]] NoteEditResult moveRegion(std::size_t index,
        double delta_seconds, std::size_t track);
    [[nodiscard]] NoteEditResult duplicateRegion(std::size_t index);
    [[nodiscard]] NoteEditResult deleteRegion(std::size_t index);

    [[nodiscard]] const std::vector<TimedMidiNote>& notes() const noexcept;
    [[nodiscard]] const std::vector<TimedMidiNote>& projectedNotes() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedIndex() const noexcept;
    [[nodiscard]] const TimedMidiNote* selectedNote() const noexcept;
    [[nodiscard]] bool select(std::optional<std::size_t> index) noexcept;
    [[nodiscard]] std::vector<std::size_t> selectedIndices() const;
    [[nodiscard]] bool isSelected(std::size_t index) const noexcept;
    [[nodiscard]] bool selectMany(std::vector<std::size_t> indices);
    void selectAll();
    [[nodiscard]] NoteEditResult toggleMuteSelected();
    [[nodiscard]] NoteEditResult splitSelected(double seconds);

    [[nodiscard]] NoteEditResult add(TimedMidiNote note);
    [[nodiscard]] NoteEditResult updateSelected(
        TimedMidiNote note,
        std::string label = "Edit note");
    [[nodiscard]] NoteEditResult moveSelected(double delta_seconds, int delta_pitch);
    [[nodiscard]] NoteEditResult resizeSelected(double duration_seconds);
    [[nodiscard]] NoteEditResult quantizeSelected(double interval_seconds);
    [[nodiscard]] NoteEditResult deleteSelected();

    [[nodiscard]] bool setScale(ScaleDefinition scale);
    void setMagnetEnabled(bool enabled);
    [[nodiscard]] const ScaleDefinition& scale() const noexcept;
    [[nodiscard]] bool magnetEnabled() const noexcept;
    [[nodiscard]] bool pitchIsInScale(std::uint8_t pitch) const noexcept;
    [[nodiscard]] std::uint8_t projectedPitch(std::uint8_t canonical_pitch) const noexcept;

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] std::string undoLabel() const;
    [[nodiscard]] std::string redoLabel() const;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void clearHistory() noexcept;

    [[nodiscard]] NoteOverlapPolicy overlapPolicy() const noexcept;
    [[nodiscard]] std::size_t overlapCount() const noexcept;

private:
    struct State {
        std::vector<TimedMidiNote> notes;
        std::optional<std::size_t> selected_index;
        std::vector<std::size_t> selected_indices;
        std::vector<PlaylistRegion> regions;
        std::size_t track_count{1};
        std::vector<TrackDefinition> tracks{{}};
    };

    struct HistoryEntry {
        State before;
        State after;
        std::string label;
    };

    [[nodiscard]] NoteEditResult commit(State next, std::string label);
    void refreshProjection();

    State state_;
    std::vector<HistoryEntry> history_;
    std::size_t history_cursor_{0};
    ScaleDefinition scale_{};
    NearestScaleProcessor scale_processor_;
    std::vector<TimedMidiNote> projected_notes_;
    bool magnet_enabled_{false};
};

[[nodiscard]] bool notesOverlap(
    const TimedMidiNote& left,
    const TimedMidiNote& right) noexcept;
[[nodiscard]] const char* noteEditResultText(NoteEditResult result) noexcept;

} // namespace hum::core
