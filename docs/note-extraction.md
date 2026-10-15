# Post-recording phrase conversion

## Pipeline and data ownership

The conversion path is:

```text
bounded AudioRecording
  → explicit channel-average mono samples
  → RawPitchEstimate frames
  → HysteresisNoteSegmenter
  → TimedMidiNote events in seconds
  → MidiSequence in ticks
  → Standard MIDI File format 0
```

The capture callback does not perform any of these conversion steps. It continues to copy normalized PCM into the bounded in-memory recording and publish level/count atomics. After Stop, `PhraseConversionProcessor` runs conversion on a Qt Concurrent worker. The UI polls a 0–100% atomic progress value and can request cooperative cancellation; the detector checks cancellation between frames.

Audio and raw estimates remain in memory. Audio is never uploaded or logged and no temporary audio file is created. Export writes only the small MIDI byte stream to the path selected by the user. Starting a successful new recording replaces the earlier take and clears its derived events. Closing the app cancels conversion and releases recording, raw-frame, and note-event memory.

## Frame interpretation and segmentation

The default detector uses a 4096-sample window and 512-sample hop. Each raw estimate records its window's first sample, frequency, periodicity-based voicing quality, RMS, and status. Derived timing treats the frame center as its evidence time and expands a confirmed support boundary by half a hop.

Defaults:

| Setting | Value | Purpose |
|---|---:|---|
| Onset confirmation | 2 consecutive reliable frames | Suppress one-frame pitch starts |
| Transition confirmation | 2 consecutive reliable frames | Suppress one-frame note changes |
| Pitch-transition hysteresis | 65 cents from the active MIDI note | Keep normal small drift/vibrato in one event |
| Maximum unreliable gap | 35 ms | Bridge short detector dropouts |
| Minimum note duration | 80 ms | Remove unstable short fragments |
| Velocity | fixed 96 | Avoid uncalibrated loudness-to-intent claims |
| Channel | 0 | MIDI channel 1 on the wire |

Leading/trailing unvoiced frames create no notes. A longer unvoiced gap ends an active note at its last supported frame and requires a new confirmed onset. This makes a clear pause capable of separating repeated pitches. A sustained transition beyond hysteresis must persist for the confirmation count; the boundary is placed at the first supporting transition frame. Pitch values outside 0–127, zero velocity, nonpositive duration, invalid channels, and tick overflow are rejected.

No scale snapping or rhythmic quantization is applied. Vibrato narrower than the transition boundary remains part of the active note. A wider or highly irregular modulation can still cross the boundary and become another note; that is a known tuning tradeoff.

## Timing and MIDI

`TimedMidiNote.start_seconds` is measured from the first captured audio frame and `duration_seconds` is positive elapsed time. Conversion uses:

```text
ticks = round(seconds × tempo_bpm × ticks_per_quarter / 60)
```

The start and end are rounded separately. The UI defaults to 120 BPM and exports at the user-selected project tempo with 480 ticks per quarter; a positive sub-tick note receives one tick. Tests cover 60, 120, and 180 BPM, and the end-to-end workflow verifies a non-default tempo. Changing tempo changes tick numbers while round-tripping to the same seconds within one-tick resolution.

The file writer emits format 0 with one track, one tempo meta event, sorted note events, and end-of-track. For events on the same tick, note-offs precede note-ons. Tests parse the bytes with a test-only reader that is independent of the writer and verify header fields, tempo, pitches, pairs, and durations.

## Delay and precision

Conversion begins only after recording stops, so there is no real-time transcription latency claim. Boundary uncertainty comes from the detector window, hop, and two-frame confirmation. The explicit automated tolerance is:

```text
one analysis window + two hops
```

At 48 kHz with 4096/512 settings this is 106.7 ms. The UI checks progress every 33 ms, which is presentation latency and is separate from analysis runtime. Measured fixture values are recorded in `note-extraction-results.md`.

## Limits

This pipeline assumes one dominant periodic vocal pitch. Breathiness, octave ambiguity, consonants, background tones, large pitch scoops, and fast ornaments can create missing, shifted, merged, or extra events. Synthetic sine fixtures do not establish human-voice quality. Uninterrupted same-pitch humming cannot reliably reveal repeated-note intent without a pause, amplitude onset, or another independent onset cue; this milestone does not invent that cue.
