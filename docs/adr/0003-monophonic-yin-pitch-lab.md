# ADR 0003: Replaceable YIN-style detector for the monophonic Pitch Lab

- Status: accepted
- Date: 2026-10-10

## Context

Pitch Lab needs low-latency local diagnostics for one hummed or sung note. It must expose frequency, nearest equal-tempered MIDI note, cents, and a defensible voicing measure without fabricating notes for silence/noise. The engine must remain testable without Qt and reusable by a later host. Model inference and expensive analysis cannot run in the microphone callback.

## Decision

Add `IPitchDetector` as the replaceable frame-detector boundary and implement `YinPitchDetector` in the Qt-free core. The implementation follows the published YIN cumulative-mean normalized difference method and parabolic period refinement. Before YIN, each frame is box-averaged toward an analysis rate of at least 12 kHz and at least six times the configured maximum pitch; a lower-rate source is never upsampled. This keeps upper vocal fundamentals sufficiently sampled on normal 44.1/48/96 kHz devices while making the Debug build faster than realtime on deterministic fixtures.

The estimator reports raw frequency, DC-removed RMS, status, and `voicing_quality = 1 − normalized_difference_at_selected_period`. This quality is a periodicity measure, not a calibrated probability. A result is reliable only when input exceeds the RMS gate, quality meets the configured minimum, and refined frequency remains inside the configured vocal range.

Live capture publishes immutable frame regions from the existing bounded recording buffer. The UI copies up to four analysis windows and submits them to `PitchLabProcessor`, which runs YIN on Qt Concurrent workers. If analysis falls behind, old windows are coalesced and display smoothing resets. The capture callback continues to perform only PCM conversion, bounded capture copy, level scanning, and atomics.

`PitchSmoother` keeps raw estimates intact and derives display frequency from a five-frame median in logarithmic MIDI-pitch space. Any unreliable raw frame immediately clears smoothing and displays `No reliable pitch`. Note and cents values are calculated only from a reliable smoothed pitch.

Stored PCM16/float32 WAV fixtures use `HumPitchFixture`, which calls the same `PitchAnalysisRunner`, detector, settings, and smoother as the app's replay path.

## Alternatives considered

| Option | Fit | License/deployment result |
|---|---|---|
| Spotify Basic Pitch | Strong later candidate for offline audio-to-MIDI and polyphonic transcription; heavier than needed for a live single-note diagnostic | Apache-2.0 repository, with model runtimes and transitive dependencies requiring a pinned packaging audit. Deferred. |
| aubio | Mature realtime pitch APIs with silence and confidence controls | GPL-3.0-or-later. Deferred until the product chooses a compatible distribution model. |
| Local YIN-style estimator | Direct fit for monophonic periodic voice, deterministic tests, small replaceable C++ implementation | No new library/model dependency. Selected. |

## Consequences

- Synthetic sine accuracy does not establish accuracy on breathy, noisy, octave-ambiguous, or expressive human voice. Those cases have a separate manual checklist.
- Box-average decimation is efficient but is not a production-grade anti-alias filter. If hardware/voice tests expose octave or high-register errors, replace it behind `IPitchDetector` or add a designed low-pass decimator.
- The detector is diagnostic only. It does not segment notes or generate MIDI events.
