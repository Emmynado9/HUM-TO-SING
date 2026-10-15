# MIDI-compatible note and timing model

The engine keeps two related representations. `TimedMidiNote` is the extraction result in seconds from the first captured audio frame. `MidiNote` is the export form inside a `MidiSequence`, using metric ticks. Both use MIDI-compatible pitch, velocity, and channel values; the seconds model preserves captured timing until an export tempo is selected.

| Field | Type and range | Meaning |
|---|---|---|
| `pitch` | integer 0–127 | MIDI key number; 60 is middle C (C4). Equal temperament uses A4 = 69 = 440 Hz. |
| `velocity` | integer 1–127 | Note-on velocity. Zero is rejected because MIDI reserves note-on velocity 0 as a note-off convention. |
| `start_tick` | non-negative integer | Absolute onset from sequence start in metric ticks. |
| `duration_ticks` | positive integer | Note length. A future exporter emits note-off at `start_tick + duration_ticks`. |
| `channel` | integer 0–15 | Zero-based internal form of MIDI channels 1–16. The initial product uses channel 0. |
| `start_seconds` | finite number ≥ 0 | `TimedMidiNote` onset relative to the first captured audio frame. |
| `duration_seconds` | finite number > 0 | `TimedMidiNote` duration before MIDI tick rounding. |
| `ticks_per_quarter` | integer 1–32767; default 480 | Pulses per quarter note (PPQ). The upper bound preserves the metric-time form of the 16-bit SMF division field. |
| `tempo_bpm` | finite positive number; default 120 | Quarter notes per minute. One global tempo is supported in this milestone's model. |

Time conversion uses:

```text
seconds = ticks × 60 / (tempo_bpm × ticks_per_quarter)
ticks   = round(seconds × tempo_bpm × ticks_per_quarter / 60)
```

`MidiTimeline::fromTimedNotes` rounds each start and end separately, then subtracts them to obtain duration. A positive note that is shorter than one tick receives one tick. Invalid input and 32-bit tick overflow return no sequence. At 120 BPM and 480 PPQ, one quarter note is 480 ticks and 0.5 seconds. This conversion is resolution rounding, not rhythmic quantization.

Detected pitches remain fractional in the raw analysis. The note segmenter rounds a stable pitch to its nearest equal-tempered MIDI note, applies its separate onset/transition policy, and emits `TimedMidiNote`. The milestone assigns fixed velocity 96. This avoids claiming that microphone amplitude precisely represents performance intent; future velocity work needs calibration and user control.

`StandardMidiFileExporter` writes Standard MIDI File format 0: a six-byte `MThd` header, one `MTrk`, a tempo meta event, explicit note-on/note-off pairs, and end-of-track. Note-offs sort before note-ons at the same tick so pause-separated repeated pitches retrigger cleanly. The writer does not use running status. Tests parse the output independently and verify note pairs, pitches, durations, division, and tempo metadata.

## Versioned project document

`ProjectDocument` schema version 1 stores `TimedMidiNote` canonical notes directly rather than storing a second MIDI or view-only note model. It also stores tempo, metric tick division, root, scale mode, Magnet state, and rhythm-snap setting. The strict local codec writes `audio_storage omitted`; source audio, raw pitch frames, and rendered instrument PCM are not embedded or referenced. See `project-files.md` for load/save behavior.

## Editable document

`NoteEditor` owns the piano roll's canonical `std::vector<TimedMidiNote>`; the widget reads and changes that model rather than holding a second view-specific MIDI representation. Loading a successful transcription copies its derived notes into the editor and resets edit history. The `PhraseConversionResult` still retains the original derived notes and raw pitch frames, and the recorder retains its original audio. Ordinary selection and editing never rerun analysis and never modify those originals. **Re-transcribe recording** is the explicit operation that replaces the editable copy.

Every committed add, delete, move, or resize is validated with `TimedMidiNote::isValid` and stored as one before/after history transaction. A drag uses an uncommitted visual preview and adds one history entry only when the pointer is released. MIDI export calls `MidiTimeline::fromTimedNotes` on `NoteEditor::notes()`, so pitch, onset, and duration corrections are present in the written file.

The overlap policy is `preserve_independent_events`. Notes at any pitches may overlap in time; editing never merges or deletes them. Touching half-open boundaries do not count as overlap. This preserves the user's data, although same-pitch overlaps may be interpreted differently by MIDI receivers that track note state only by channel and pitch.

## Scale-guided export projection

`NoteEditor::notes()` remains the canonical editable phrase. `NoteEditor::projectedNotes()` is a derived copy used by both the piano roll and MIDI export. With Magnet OFF, projected and canonical notes are identical. With Magnet ON, only `pitch` may differ: each canonical pitch is independently mapped through the selected `ScaleDefinition`. Start, duration, velocity, channel, note count, and ordering are copied exactly.

Scale selection and the Magnet toggle are view/playback/export settings rather than destructive note-edit commands, so they do not enter undo history. Undo and redo restore canonical note snapshots and then recompute the projection using the currently selected scale. Changing key or mode never consumes a previously projected pitch. Switching Magnet OFF therefore reveals the latest canonical edits. Built-in playback and export both consume this projection, so visible, audible, and exported pitches agree.
