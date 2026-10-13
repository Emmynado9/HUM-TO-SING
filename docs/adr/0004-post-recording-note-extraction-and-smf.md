# ADR 0004: Post-recording note segmentation and Standard MIDI File export

- Status: accepted
- Date: 2026-10-13

## Context

A stopped monophonic recording must become inspectable note events and a valid MIDI file. The milestone favors repeatable offline conversion over immediate transcription. It must retain raw detector evidence, preserve expressive timing by default, avoid doing analysis or file I/O in the capture callback, and stay reusable outside Qt.

## Decision

Add a UI-free `PhraseConversionPipeline` with replaceable `IPitchDetector` and `INoteSegmenter` boundaries. It mono-converts the immutable `AudioRecording`, runs the existing local YIN detector over complete frames, retains every `RawPitchEstimate`, and passes those estimates to `HysteresisNoteSegmenter`. Derived notes use the separate `TimedMidiNote` model in seconds.

The default segmenter requires two consecutive frames for an onset and two for a pitch transition. Pitch remains in the active note while it is within 65 cents of that note. An unreliable region up to 35 ms is bridged; a longer region closes the active note and can support a repeated note after voicing returns. Notes shorter than 80 ms are removed. These settings are deliberately conservative and can later move behind user presets.

The milestone uses fixed MIDI velocity 96. Microphone RMS depends on distance, gain, room, vowel, and hardware, so amplitude-derived velocity would imply a musical-intent accuracy that has not been calibrated.

`MidiTimeline::fromTimedNotes` converts absolute seconds to ticks only when a tempo and ticks-per-quarter value are supplied. It rounds the start and end independently and never snaps to a beat or scale. `StandardMidiFileExporter` writes format 0 with one tempo event and explicit note-on/off messages.

The Qt `PhraseConversionProcessor` runs the pipeline through Qt Concurrent. It exposes atomic progress and cooperative cancellation. The main window starts conversion after Stop and polls progress on the existing 33 ms UI timer. The audio callback remains unchanged.

## Consequences

- Original timing remains available in seconds alongside the tick representation and raw pitch frames.
- Windowed pitch evidence limits boundary precision. With the default 4096-sample window and 512-sample hop at 48 kHz, the explicit test tolerance is one window plus two hops, 106.7 ms.
- Clear silence can separate repeated pitches. Uninterrupted same-pitch humming contains no reliable repeated-note intent, so it remains one event unless a future onset detector supplies independent evidence.
- One global tempo is written. Tempo maps seconds to ticks; it does not alter the audio or inferred note boundaries.
- Human humming, MIDI import in a DAW, Windows, and macOS remain manual/unverified on this cloud machine.

## Alternatives considered

- Basic Pitch remains useful for later offline transcription, especially polyphonic material, but its model runtimes add packaging complexity and its broader task is unnecessary for this deterministic monophonic stage.
- aubio has note/pitch facilities but remains GPL-3.0-or-later; linking it would constrain distribution before this repository has chosen a compatible license.
- Immediate note emission during capture would reduce perceived delay but couples segmentation to incomplete future context and makes gap/transition decisions less reliable. It is deferred.
