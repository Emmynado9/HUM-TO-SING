# Cumulative MVP acceptance checklist

## Required in this milestone

- [x] Empty-repository baseline and missing blueprint attachment recorded.
- [x] Project guardrails persisted in `AGENTS.md`.
- [x] Desktop shell presents the Hum → Edit → Play → Export workflow.
- [x] Unimplemented controls and milestone status are visibly identified.
- [x] Microphone capture, analysis, note extraction, scale processing, MIDI, UI, playback, and export are separated by interfaces/models.
- [x] Core library has no Qt/UI dependency and builds independently.
- [x] MIDI-compatible note/timing model and conversion rules are documented and tested.
- [x] Build, run, test, and dependency installation commands are documented.
- [x] Architecture and current dependency-license evidence are documented.
- [x] Debian 13 debug build completed on the available x86_64 platform.
- [x] Core and desktop tests pass, including independent note-editor and offscreen piano-roll interaction suites.
- [x] GUI process created its window and completed the offscreen launch smoke check.
- [x] The final desktop CTest run passes all currently registered core, integration, project-store, offscreen UI, and launch tests.

## Implemented later milestones and remaining work

- [x] Microphone permission/device selection and bounded in-memory audio capture.
- [x] Pitch analysis and voiced/unvoiced quality handling.
- [x] Conservative post-recording monophonic note segmentation.
- [x] Standard MIDI File format-0 export with structural tests.
- [x] Editable piano-roll UI with selection, add/delete, drag move/resize, scroll, zoom, and undo/redo.
- [x] Optional rhythmic timing snap is explicit and disabled by default; scale snapping remains separate.
- [x] Edited canonical notes feed MIDI export while original audio and transcription evidence remain intact.
- [x] All 12 roots and Major/Natural minor guidance are selectable.
- [x] In-scale and out-of-scale notes have color and text/line-style cues.
- [x] Magnet derives reversible visible/export pitches without replacing canonical pitches.
- [x] Key changes recompute from canonical notes and do not transpose the melody.
- [x] Offline procedural instrument playback uses sample-clocked note scheduling, Play/Stop, and an audio-clock playhead.
- [x] Playback/export consume the same canonical-or-Magnet projection, and lifecycle changes stop the sink.
- [x] Versioned project files atomically save/load canonical notes, tempo, key/scale, Magnet, and rhythm-snap settings.
- [x] Project UI states that original audio is omitted, guards unsaved changes, and handles corrupt/unsupported/save failures.
- [x] Debian 13 package and archive generation are configured with explicit dynamic Qt runtime dependencies.
- [ ] Native package installation, microphone/speaker workflow, output-device changes, accessibility, and DAW import on physical hardware.
- [ ] Package signing, owner-selected project license, Windows packaging, and macOS permission/signing manifests.
- [ ] Windows and macOS builds and launch tests.
- [ ] Chords, multiple tracks, beatboxing, generative accompaniment, and VST3.

## Platform evidence

| Platform | Build | Core tests | App launch | Packaging |
|---|---|---|---|---|
| Debian 13 x86_64 | Executed | Executed | Executed offscreen, including piano-roll/playhead interaction tests | DEB/TGZ generated and extracted smoke-tested; native install not run |
| Windows | Untested | Untested | Untested | Untested |
| macOS | Untested | Untested | Untested | Untested |
