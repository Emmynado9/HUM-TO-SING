# Microphone milestone acceptance checklist

Recorded on 2026-10-10.

- [x] Existing core and GUI smoke baselines were run before editing and passed.
- [x] Input devices can be enumerated, selected, and refreshed through Qt Multimedia.
- [x] Permission states and missing/disconnected devices have actionable UI messages.
- [x] Record, Stop, duration, level meter, clipping warning, and source metadata are shown.
- [x] `RecordingController` prevents duplicate sessions and recovers from device failure.
- [x] Capture uses a preallocated 30-second buffer and keeps actual rate/channels/sample count.
- [x] Mono conversion and explicit playback resampling rules are documented and tested.
- [x] The original take can be prepared asynchronously and played through the current output.
- [x] Silence, very short input, repeated controls, limit stop, disconnect, and shutdown are handled.
- [x] Fake-source tests cover state, metadata, 44.1/48/96 kHz, stereo-to-mono, limit, clipping, recovery, timing, and cleanup.
- [x] Audio remains in memory and is neither uploaded nor written to a temporary file.
- [x] The complete Linux build, independent core tests, recording tests, and offscreen launch test pass.
- [ ] Live input and output are manually verified; the build environment exposes no audio device.
- [ ] Windows and macOS builds and hardware behavior are tested.
- [ ] Pitch detection and note extraction are implemented; both belong to the next milestone.
