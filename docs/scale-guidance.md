# Key, scale, and Magnet behavior

## Supported scales

The root selector covers all pitch classes: C, C#/Db, D, D#/Eb, E, F, F#/Gb, G, G#/Ab, A, A#/Bb, and B. Enharmonic names share one MIDI pitch class. The MVP supplies:

| Scale | Semitone intervals from root |
|---|---|
| Major | 0, 2, 4, 5, 7, 9, 11 |
| Natural minor | 0, 2, 3, 5, 7, 8, 10 |

`ScaleDefinition` stores a root plus an arbitrary unique interval vector, so later modes do not require another note representation.

Selecting a key does not transpose the melody. It changes row highlighting and, only while Magnet is ON, the derived visible/export pitches. There is no automatic key detection.

## Exact mapping rule

An in-scale pitch remains unchanged. For an out-of-scale pitch, search outward by one semitone, then two, and so on. At each distance, test the lower pitch before the upper pitch. Equal-distance ties therefore choose the lower valid scale pitch. If the lower or upper candidate falls outside MIDI 0–127, skip it. Applying the mapping again is idempotent because its result is already in scale.

Examples in C Major:

- C#4 (61) maps to C4 (60), not D4 (62).
- D#4 (63) maps to D4 (62), not E4 (64).
- F#4 (66) maps to F4 (65), not G4 (67).

## Canonical and projected phrases

The editor keeps the songwriter's canonical note events. A separate projection supplies the piano-roll positions and MIDI export:

| Action | Canonical phrase | Visible/export projection | Undo entry |
|---|---|---|---|
| Select key or scale | Unchanged | Recomputed from canonical notes | No |
| Magnet OFF | Unchanged | Exact canonical pitches | No |
| Magnet ON | Unchanged | Closest selected-scale pitches | No |
| Add/move note with Magnet OFF | Stores chosen pitch | Same pitch | Yes |
| Add/move note with Magnet ON | Stores the chosen grid pitch, then reprojects it | Closest selected-scale pitch | Yes |
| Resize/delete | Normal canonical edit | Recomputed | Yes |
| Undo/redo | Restores canonical edit snapshot | Recomputed under current key/Magnet | Performs history action |
| Re-transcribe | Replaces canonical notes explicitly | Recomputed under current settings | Resets note history |

This design never feeds a previously snapped pitch back into another key calculation. Changing C Major to D Major always starts from the canonical pitch. Turning Magnet off reveals every detected or manually edited pitch still present underneath.

Magnet changes no start time, duration, velocity, channel, event count, or order. It never enables rhythmic snapping. Rhythm snap is the separate control labeled **Rhythm snap** and remains off by default.

## Visual guidance and export

Scale rows receive a visible highlight and an `·` marker on the pitch axis. Canonical out-of-scale notes use a muted fill, dashed outline, and `OUT` or `!` label with Magnet OFF. With Magnet ON, they are placed at the derived pitch and labeled `MAG`; the tooltip states both original and visible notes. This supplies non-color cues.

MIDI export consumes the same projected note vector as the piano roll. With Magnet OFF it exports canonical pitches; with Magnet ON it exports the shown scale pitches. The original recording, raw pitch frames, original transcription, and canonical editor phrase are not modified by scale settings.

Transposition, automatic key detection, chord inference, and chord-scale advice remain out of scope.
