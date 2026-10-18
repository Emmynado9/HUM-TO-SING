# Independent Piano Roll Sing to MIDI

Implementation checkpoints A–F are complete. See [executed checks and measured results](sing-to-midi-results.md) for outcomes and remaining limits. Additional integration files are `src/app/vocal/VocalModelAssets.hpp` (bundled asset access), `src/tools/VocalFixtureMain.cpp` and `scripts/evaluate-vocal.py` (local reproducible evaluation).

## Checkpoint A — inspection and isolation

The user requested this as a separate alternative to the existing converter. The application is C++20 with Qt 6.8.2 Widgets, Multimedia and Concurrent, built by CMake for Intel macOS 13. Its Qt-free core owns canonical notes, track metadata, undo, scale projection, playback data and MIDI serialization. The existing converter uses local YIN; Basic Pitch is not installed. Pitch Lab was already removed at the user's request and will remain removed.

Baseline commands executed successfully:

```sh
LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2
LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure
```

All 20 existing tests passed. Logs: `build/sing-midi-baseline-build.log` and `build/sing-midi-baseline-tests.log`. The checkout was clean before this work.

## Proposed boundary and files

```text
Piano Roll SingToMidiPanel
  -> IsolatedMicRecorder (separate capture instance, buffers and cancellation)
  -> hum::vocal AudioBuffer
  -> NeuralPitchInference (local ONNX CPU session)
  -> PitchFrames -> PhraseSegmenter / MusicalTokens -> NoteEvents
  -> optional cleanup, scale and time correction -> preview
  -> PianoRollInsertAdapter -> NoteEditor::appendRegion(notes, origin, selectedTrack)
```

Only the final insertion adapter translates preview events into canonical `TimedMidiNote` objects. `appendRegion` already appends a phrase as one undo transaction, so neither the shared note model nor its editing API needs modification. The host adapter supplies selected track, instrument, BPM, PPQ, key/scale and a captured recording origin in seconds. Tick conversion stays with `MidiTimeline`, using the actual project settings.

Planned new files:

- `include/hum/vocal/transcription.hpp`, `src/vocal/transcription.cpp`: independent data contracts, validation, confidence gating, event tokenization and reconstruction, presets and reversible preview correction.
- `include/hum/vocal/neural_pitch.hpp`, `src/vocal/neural_pitch.cpp`: independent neural inference, mono/resampling, model loading and cancellation.
- `src/app/vocal/IsolatedMicRecorder.hpp/.cpp`: its own recorder state and bounded audio buffer; a separately instantiated public audio-device wrapper supplies samples without changing the existing recorder.
- `src/app/vocal/SingToMidiPanel.hpp/.cpp`: capture, conversion worker, controls, preview audition, discard and insertion.
- `src/app/vocal/VocalPreviewWidget.hpp/.cpp`: temporary, read-only note preview in the new panel; no preview events enter the canonical editor.
- `src/app/vocal/PianoRollInsertAdapter.hpp/.cpp`: sole mutation boundary to existing tracks and one-step undo.
- `tests/vocal_transcription_tests.cpp`, `tests/vocal_inference_tests.cpp`, `tests/vocal_capture_tests.cpp`, `tests/sing_to_midi_ui_tests.cpp`: core, backend, recorder and host regression checks.
- `cmake/NeuralVocal.cmake`, `scripts/prepare-neural-vocal.py`, `resources/vocal/`: pinned model/runtime preparation and license notices.
- `docs/sing-to-midi.md`, `docs/sing-to-midi-results.md`: usage, implementation decisions, measured results and remaining limitations.

Existing files expected to change: `CMakeLists.txt`, `src/app/MainWindow.cpp` (Piano Roll entry and adapter callbacks only), `src/app/main.cpp` (smoke coverage), `scripts/build-intel-mac.sh`, `README.md`, `docs/dependencies-and-licenses.md` and `SOURCE-MANIFEST.sha256`. Existing YIN, phrase conversion, recorder, MIDI schema and piano editing behavior remain outside the implementation boundary.

## Backend decision

Evaluate native CPU inference with [SwiftF0](https://github.com/lars76/swift-f0), pinned to `d1ba77fe5331310b778ebe126b6cb3656d41d24f`, using [ONNX Runtime 1.20.1](https://github.com/microsoft/onnxruntime/releases/tag/v1.20.1). The inspected model is 135,090 bytes and accepts mono 16-kHz samples; the upstream wrapper returns one pitch/confidence estimate per 16 ms. The official Intel Mac runtime archive is 8,940,679 bytes compressed. Both projects declare MIT licenses; package their notices and runtime third-party notices. This avoids a Python/TensorFlow/PyTorch runtime in the desktop app. System Python has PyTorch but no installed pitch model or ONNX runtime; it is not an application dependency.

Native model loading and inference on this Mac must pass before adopting the backend. Report measured processing time and package size rather than assuming upstream performance. Downloads happen during dependency preparation; recording and inference require no network. Missing runtime/model produces an explicit unavailable-feature error, never a silent fallback to the old converter.

## Sequential implementation checks

1. **B — recorder/panel:** device selection, permission, level/time, bounded capture, stop/cancel/close and disconnect handling. Build and test before inference integration.
2. **C — inference/events:** normalized pitch frames, confidence and silence gates, time-based smoothing/hysteresis, repeated-articulation handling, transparent start/hold/slide/end/rest events. Build and test sustained, repeated, fast and noisy fixtures.
3. **D — correction/preview:** Natural/Clean/Tight, independent Scale Snap and quantization controls, original result retained for toggling, temporary preview and local audition. Quantization uses absolute project time, including a nonzero recording origin.
4. **E — insertion:** append into the track selected at insertion; validate that the current project/track still exists, preserve existing notes and instrument/mute settings, one undo/redo, normal playback/export.
5. **F — evaluation/package:** existing regressions, native UI checks, failure/cancellation coverage and Intel Mac packaging. Evaluate several [Vocadito](https://zenodo.org/records/5578807) human vocal recordings against supplied hand-labelled notes, reporting pitch/onset/duration errors. Its CC BY 4.0 evaluation audio stays under ignored build storage and is not shipped with the app or uploaded.

## Known limits and safeguards

- Monophonic offline conversion only; no generative accompaniment, chords, model training or streaming claims.
- The existing note/MIDI model has no pitch-bend events. Disable bend output and document fixed-note approximation of slides/vibrato.
- Keep new controls/preferences separate from the old engine and global Magnet. Explain when the host's current Magnet setting changes the eventual arrangement projection.
- Freeze the time origin at Record; capture uses the first audio sample as time zero. Changing the playhead afterward does not silently move the take.
- Prevent competing recording/preview operations at the host boundary, cancel stale workers on close/project replacement, and avoid callbacks into destroyed widgets.
- No promise of exact voice reproduction or improved accuracy until measured. Singing timbre is not represented by MIDI notes.
