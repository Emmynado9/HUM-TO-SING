# Pitch Lab milestone acceptance checklist

Recorded on 2026-10-10.

- [x] Unchanged core and desktop baseline builds/tests passed before editing.
- [x] Basic Pitch and aubio use cases and upstream licenses were reviewed before selection.
- [x] The detector is behind `IPitchDetector` and adds no library, model, API, or cloud dependency.
- [x] Live analysis runs on workers and never inside the microphone callback.
- [x] Frame size, hop size, input gate, and vocal range are configurable.
- [x] Raw estimates remain separate from five-frame display smoothing.
- [x] Silence/unreliable input explicitly displays `No reliable pitch`.
- [x] Frequency, nearest note, cents from that note, RMS, and periodicity-based voicing quality are exposed.
- [x] A local stored-WAV fixture utility runs the same detector/runner path.
- [x] Deterministic fixtures cover A4, range tones, silence, fixed-seed noise, amplitude changes, vibrato, and low-level input.
- [x] Clean steady-tone median error stays below 20 cents after the documented 150 ms warm-up.
- [x] Detector-window, hop, worker, and UI-update latency are distinguished.
- [ ] Live humming accuracy, breathiness, vibrato behavior, and octave-error rate are verified on physical hardware.
- [ ] Windows and macOS builds and audio behavior are tested.
- [ ] Note segmentation, piano-roll editing, and chord recognition are implemented; these remain later milestones.
