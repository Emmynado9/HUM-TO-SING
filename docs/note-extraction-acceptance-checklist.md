# Note extraction milestone acceptance checklist

## Automated and implementation evidence

- [x] Conversion runs after Stop on a worker and leaves the audio callback unchanged.
- [x] Raw pitch/voicing frames remain separate from derived note events.
- [x] Notes include MIDI pitch, onset seconds, duration seconds, velocity, and channel.
- [x] Leading/trailing silence, voiced pauses, short gaps, minimum duration, vibrato, hysteresis, and repeated pitches with pauses have deterministic tests.
- [x] Seconds-to-ticks conversion is tested at 60, 120, and 180 BPM.
- [x] No scale snapping or rhythmic quantization is applied.
- [x] The UI shows conversion progress, cancellation, empty/error text, and inspectable note events; the later piano-roll milestone replaced the read-only table.
- [x] Standard MIDI File format 0 output is parsed independently in tests for tempo, division, note-on/off pairs, pitch, ordering, and duration.
- [x] Local export cleanup is tested.
- [x] Qt-free core tests and the desktop build/smoke test pass.

## Manual evidence still required

- [ ] Record and convert real humming on available microphone hardware.
- [ ] Verify audible original playback and compare event boundaries with the performance.
- [ ] Exercise Cancel on a long take and close the app during conversion.
- [ ] Export through the native file dialog and inspect the file in at least one external MIDI tool.
- [ ] Import into a DAW and record the product/version and observed timing. No successful DAW import is currently claimed.
- [ ] Repeat on Windows and macOS. Both platforms are untested.
