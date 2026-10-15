# ADR 0006: Reversible scale projection over canonical edits

- Status: accepted
- Date: 2026-10-13

## Context

A songwriter needs key and scale guidance without losing detected or manually corrected pitches. The same effective pitches must appear on the piano roll and in exported MIDI. Key changes must not compound prior snapping, and disabling Magnet must recover the underlying idea. Rhythm snapping, transposition, automatic key detection, and chord inference are different operations.

## Decision

Keep `NoteEditor::notes()` as the canonical editable `TimedMidiNote` phrase and maintain `projectedNotes()` as a derived view. `ScaleDefinition` remains an extensible root plus interval vector. `ScaleMode` factories currently provide Major `{0,2,4,5,7,9,11}` and Natural minor `{0,2,3,5,7,8,10}` for any of the 12 roots. `NearestScaleProcessor` copies all events and changes only pitch.

Magnet OFF makes the projection identical to the canonical phrase. Magnet ON independently maps every pitch to the nearest valid MIDI pitch in the selected scale. The search expands one semitone at a time and checks the lower candidate first, so an equal-distance tie resolves downward. At MIDI 0 or 127, an unavailable candidate is skipped and the valid in-range candidate wins.

Scale and Magnet settings are not note-history commands. Every change and every undo/redo recomputes from canonical notes under the current setting. The piano roll and MIDI exporter both consume the projection. The editor, selection, and history continue to consume canonical notes.

While Magnet is ON, a pitch drag interprets the drop row as the new canonical/manual pitch, commits one normal move transaction, and immediately projects that value. A drop on an out-of-scale row may therefore appear at a neighboring scale row while Magnet remains on. Undo restores the preceding canonical pitch and then reapplies the current scale. Timing edits retain their existing optional rhythm-snap behavior and never arise from Magnet.

## Consequences

- Switching Magnet OFF reveals the current canonical phrase without reconstructing or approximating it.
- Changing key or mode can change visible/export pitches but never transposes or rewrites the canonical melody.
- Start, duration, velocity, channel, event count, and ordering are unchanged by projection.
- Export matches the visible Magnet state.
- Out-of-scale notes remain identifiable with muted fill, dashed outlines, and text/tooltips rather than color alone.
- Major and Natural minor are the only supplied patterns. Adding another mode requires a new factory case and UI option, while the interval-based processor remains unchanged.
