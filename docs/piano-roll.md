# Piano-roll editing contract

## Data ownership and isolation

The piano roll uses `NoteEditor` as its one editable note document. Those notes are the existing MIDI-compatible `TimedMidiNote` values in seconds. `PianoRollWidget` does not define a second note type or keep a shadow list. A drag preview is temporary and never becomes project state until release.

Post-recording conversion retains three separate forms:

1. the original bounded `AudioRecording`;
2. the original raw pitch frames and derived notes in `PhraseConversionResult`;
3. an editable copy loaded into `NoteEditor`.

Note edits only affect the third form. They never call the pitch detector or segmenter. MIDI export reads the editor's current canonical-or-Magnet projection, which is also the view rendered by the piano roll. Choosing **Re-transcribe recording** explicitly reruns conversion and replaces the editable document; recording a new take intentionally starts a new phrase and clears the previous editor.

## Coordinates and controls

Time content coordinates are `x = seconds × pixels_per_second`. Pitch rows run downward from MIDI 127 to MIDI 0: `y = (127 − pitch) × pixels_per_semitone`. The inverse uses the containing semitone row. The viewport adds a fixed 72-pixel labeled pitch axis and 28-pixel time header, then applies scrollbar offsets.

Time zoom is clamped to 50–520 pixels per second and pitch zoom to 10–34 pixels per semitone. Sliders expose both. Ctrl+wheel changes time zoom, Shift+wheel changes pitch zoom, and ordinary wheel/scrollbar behavior scrolls. Time labels are in seconds because transcription preserves recorded timing and the current UI has no user-selected tempo.

Interactions:

- click a block to select it;
- drag the body to move onset and pitch;
- drag the right-edge handle to change duration;
- double-click empty grid space or choose **Add note** to create a 500 ms, velocity-96 note;
- choose **Delete** or press Delete/Backspace to remove the selected event;
- choose **Undo**/**Redo** or use platform-standard shortcuts;
- hover for the note name, MIDI number, seconds, and plain-language editing help.

The visible preview clamps pitch to 0–127, onset to zero or later, and duration to at least 20 ms. The core validates again before commit. Selection itself is not a content edit and does not enter history. Add, delete, move, and resize do. One pointer drag commits one before/after transaction.

## Timing snap, scale guidance, and overlap rules

Rhythm snap is off by default. When enabled, it rounds edited time values to 0.125, 0.250, or 0.500 second intervals. It is explicitly a time-grid control and remains independent of pitch guidance.

The selected Major or Natural minor scale highlights valid rows. Canonical notes outside it use a muted fill, dashed border, and `OUT` or `!` label, so meaning does not depend on color. With Magnet ON, the block is positioned at its derived pitch and labeled `MAG`; its tooltip names both the original and visible pitch. See `scale-guidance.md` for mapping and edit semantics.

All valid overlaps are preserved. No edit silently deletes, truncates, or merges another note. The status reports the number of overlapping time pairs. Touching endpoints do not count. Same-pitch overlaps can produce receiver-specific note-off behavior after export and should be corrected manually if unintended.

## Empty and dense documents

An empty editor displays an invitation to record and stop a melody or double-click to add a note. The content timeline has an eight-second minimum and grows two seconds beyond the latest note. The pitch axis always covers MIDI 0–127. Painting skips note blocks outside the viewport, while hit testing and snapshot history remain linear in the note count. The overlap status uses an O(n log n) interval sweep.
