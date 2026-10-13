# Foundation milestone acceptance checklist

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
- [x] Core test executable passed 24 assertions.
- [x] GUI process created its window and completed the offscreen launch smoke check.
- [x] CTest passed both registered tests.

## Deliberately deferred

- [ ] Microphone permission/device selection and audio capture.
- [ ] Pitch analysis and voiced/unvoiced confidence handling.
- [x] Conservative post-recording monophonic note segmentation.
- [x] Standard MIDI File format-0 export with structural tests.
- [ ] Editable piano-roll UI.
- [ ] Playback synthesis for the extracted MIDI notes.
- [ ] Project-file serialization.
- [ ] Distribution-ready dependency bundling, installer, signing, and microphone permission manifests.
- [ ] Windows and macOS builds and launch tests.
- [ ] Chords, multiple tracks, beatboxing, generative accompaniment, and VST3.

## Platform evidence

| Platform | Build | Core tests | App launch | Packaging |
|---|---|---|---|---|
| Debian 13 x86_64 | Executed | Executed | Executed offscreen | CPack archive scaffold only |
| Windows | Untested | Untested | Untested | Untested |
| macOS | Untested | Untested | Untested | Untested |
