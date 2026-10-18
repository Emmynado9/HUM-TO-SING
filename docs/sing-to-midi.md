# Sing to MIDI — a separate Piano Roll alternative

Select a Playlist track, position the playhead, open **Piano roll**, and choose **Sing to MIDI**. The existing **Hum → MIDI** recorder and YIN converter are still available through their original controls.

1. Choose a microphone and press **Record**. Grant microphone permission if requested. Sing or hum one unaccompanied melody and watch the input meter. Each take is limited to 30 seconds.
2. Press **Stop recording**, then **Convert to MIDI**. Conversion runs locally after recording; it does not insert notes automatically.
3. Inspect the blue preview notes and use **Play Preview** to hear the selected track's instrument. Grey preview notes are existing notes on the selected track. The preview is read-only.
4. Try **Natural**, **Clean** or **Tight**. Natural retains faster changes; Clean uses more smoothing; Tight requires more stable pitch changes and can lose short notes. **Scale Snap** is independent of the main Magnet button. **Time quantize** is Off, 1/4, 1/8 or 1/16. Presets do not enable quantization automatically. Changing these controls reprocesses the original contour, so switching correction off restores the uncorrected take.
5. Choose **Insert to Selected Track**. This appends one phrase region, preserves existing notes and track settings, and can be undone or redone in one action. Close the panel to edit the inserted notes in the normal piano roll, play the arrangement, save or export MIDI.

The first captured audio sample aligns with the project playhead position captured when Record starts. Initial silence stays in the take. The panel displays this origin. Quantization uses the project's absolute beat grid and current BPM; MIDI export uses the project's actual PPQ. Leave room for the take by choosing an origin at or before 570 seconds in the 600-second editor timeline.

**Discard** and closing the panel release the in-memory take and temporary preview. They do not remove an already inserted region; use Undo for that. **Cancel conversion** retains the take for retry and prevents a late worker result from inserting or reviving a discarded preview. The panel is modal while open so recording/preview cannot compete with the existing recorder or arrangement transport. Opening it stops playback while retaining the playhead. It does not change tempo, metronome, instrument, track mute or global Magnet.

If project Magnet is on, the normal arrangement projection can still snap inserted notes during playback/export. A message in the panel makes this explicit. Preview audition remains audible even if the destination track is muted; arrangement playback/export continue to respect that mute.

## Engine and isolation

The new `hum::vocal` module runs the pinned [SwiftF0 model](https://github.com/lars76/swift-f0/tree/d1ba77fe5331310b778ebe126b6cb3656d41d24f) in native CPU ONNX Runtime. It uses its own recorder session, audio buffer, configuration, worker, raw contour, musical tokens and preview. It never calls the existing YIN or phrase-conversion service. Its only project mutation is the insertion adapter's call to the existing `NoteEditor::appendRegion` transaction. No shared note schema, editor API or old conversion algorithm was changed.

The model consumes 16-kHz mono audio. Other input rates use a local windowed-sinc resampler. Analysis returns a pitch and confidence every 16 ms. The converter uses confidence entry/release thresholds, an amplitude gate, time-based median smoothing, pitch-change hysteresis and bounded audible-gap repair. An energy dip and re-onset can split repeated notes. The token sequence contains NOTE_START, NOTE_HOLD, PITCH_SLIDE, NOTE_END and REST; reconstruction decodes these into fixed-pitch notes with velocity 96. These are transparent symbolic events, not generative or newly trained neural tokens.

| Preset | Smoothing | Audible gap bridge | Minimum note | Pitch confirmation |
|---|---:|---:|---:|---:|
| Natural | 35 ms | 40 ms | 60 ms | 45 ms |
| Clean | 55 ms | 65 ms | 70 ms | 60 ms |
| Tight | 85 ms | 95 ms | 85 ms | 85 ms |

Internal sensitivity is validated in 0–1 and maps to an entry threshold of `0.75 - 0.5 * sensitivity`; the preset value 0.5 gives 0.5. An established agreeing pitch can continue at 75% of that threshold. RMS below 0.001 is gated. Gap bridging requires audible frames, agreeing anchors and no credible new onset; digital silence remains a rest. Configurable internal limits are covered by tests; the MVP UI exposes presets rather than a long advanced panel.

## Availability and limits

The packaged Intel Mac neural runtime requires **macOS 13.3 or later**. It is loaded only when this separate feature converts a take. The original app/engine keeps its existing macOS 13.0 minimum. A missing, incompatible or corrupt neural dependency produces an explicit error; it never silently invokes YIN. Other platforms can build the Qt-free engine tests and provide their own compatible native ONNX Runtime via `HUM_ONNXRUNTIME_ROOT`; their neural packaging has not been validated.

All conversion, recording and preview are offline. The app embeds the approximately 132-KiB model and bundles the native runtime; it does not download anything at runtime or upload audio. Pinned dependency downloads happen only during build preparation. No Python, TensorFlow, PyTorch, paid API, account or cloud backend is needed by the finished Mac app.

This is monophonic transcription, not chord recognition or generative accompaniment. The current MIDI model has no pitch-bend automation, so that control is unavailable. Slides and vibrato are approximated by fixed-pitch notes; timbre is not transcribed. Background music, clipping, unstable intonation, breath noise and weak articulations can still cause errors. Review and edit the preview rather than assuming exact reproduction. [Measured results](sing-to-midi-results.md) include human recordings and known failure cases.

## Building and diagnostics

`scripts/build-intel-mac.sh` prepares checksum-pinned dependencies, builds/tests, bundles the runtime/notices and verifies actual neural inference inside the deployed app. To prepare dependencies independently:

```sh
python3 scripts/prepare-neural-vocal.py
```

For local WAV evaluation, the optional Qt-free `HumVocalFixture` prints note CSV; a fourth argument writes pitch-frame CSV. The `yin` mode is only a benchmark comparator, not an application fallback:

```sh
build/macos-intel/bin/HumVocalFixture sample.wav natural
build/macos-intel/bin/HumVocalFixture sample.wav clean frames.csv
build/macos-intel/bin/HumVocalFixture sample.wav yin
```

The app's `--vocal-backend-check` generates a synthetic tone in memory to validate its bundled model/runtime without opening the microphone. Dependency notices and hashes are in `resources/vocal/`.
