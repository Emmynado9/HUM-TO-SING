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

`StandardMidiFileExporter` writes Standard MIDI File format 0: a six-byte `MThd` header, one `MTrk`, a tempo meta event, explicit note-on/note-off pairs, and end-of-track. Note-offs sort before note-ons at the same tick so pause-separated repeated pitches retrigger cleanly. The writer does not use running status. `Project` remains a serialization-friendly in-memory container; a project-file format is still deferred.
