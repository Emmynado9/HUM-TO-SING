# Sing to MIDI verification — 2026-10-18

The separate Piano Roll alternative is implemented with native SwiftF0/ONNX CPU inference. The original Hum → MIDI/YIN converter remains available. No existing core, recording wrapper, phrase converter or piano-roll editing algorithm was changed. The only new canonical-note mutation is an undoable append through the insertion adapter.

## Commands executed and observed results

Environment: Intel Mac, macOS 13.7.8, Qt 6.8.2, Apple Command Line Tools, CMake 3.31.10. Build/test commands used `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8`.

| Check | Command | Observed result |
|---|---|---|
| Before editing | `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2`, then `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure` | Build succeeded; 20/20 baseline tests passed. |
| Final desktop | Same build and CTest commands | Build succeeded; 26/26 tests passed, including inference using the app's bundled model/runtime. |
| Final Qt-free core | `build/macos-tools/venv/bin/cmake --build --preset core`, then `build/macos-tools/venv/bin/ctest --preset core` | Build succeeded; 15/15 tests passed. |
| Native new panel | `QT_QPA_PLATFORM=cocoa build/macos-intel/bin/hum_sing_to_midi_ui_tests --native-preview-check build/sing-midi-native/preview.png` | Zero UI failures; preview playback started/stopped through the native output path. Captured layout was inspected. Input was injected, not a live microphone. |
| Existing native workspace | `python3 build/track-workspace-checks/run.py` | 13 checks passed: selected-track editing, grey references, save/load metadata, deletion/undo, seeking, pause/resume and retaining playback position while editing. This local harness is an ignored build artifact; the committed CTest suites remain the reproducible automated coverage. |

Logs are under ignored build storage: `sing-midi-baseline-*`, `sing-midi-release-*`, `sing-midi-final-core-*`, `sing-midi-native/legacy-workflow.log` and `sing-midi-native/results-final.log`.

Sequential implementation checkpoints were built/tested before proceeding: B (capture/panel), C (inference/events), D (correction/preview), E (insertion), then F (robustness/evaluation/package). One F-stage synthetic repeated-sine fixture initially produced four fragments instead of two. Adding a lower continuation-confidence threshold for an established agreeing pitch fixed that regression while preserving true rests and explicit re-onsets. The final suites passed after this change; the human benchmark below was rerun against the final implementation.

## Coverage

- Capture: separate buffer/session, stereo mixing, sample-based duration, bounded takes, empty input, invalid/NaN samples, permission denial, busy/missing devices, disconnect, retry, stop and discard.
- Analysis: sustained notes across inference batches and 16/44.1/48-kHz input; confidence dips, vibrato, repeated articulation, 100-ms note changes, silence/noise, time-based gap repair, real rests, clipping, invalid settings/timestamps and cancellation. Tokens reconstruct the preview notes; bends use the documented fixed-pitch approximation.
- Dependencies: missing runtime in a fresh process, absent/corrupt model, cancellation before and between inference batches, and actual bundled neural inference. Errors are explicit and never invoke YIN.
- Preview/insertion: real neural inference with injected capture, independent scale correction restored when switched off, quantization at a nonzero 3.37-second origin and 123 BPM, no preview changes to undo history, selected destination, preserved existing notes/type/instrument/mute, 960-PPQ export, one Undo/Redo, double-insert guard, stale/cancelled result rejection and close/discard cleanup.

## Human-vocal pilot

Evaluated the first three numbered [Vocadito recordings](https://zenodo.org/records/5578807), without selecting clips by outcome, against annotator A1's note labels. Durations were 33.21, 35.21 and 21.74 seconds at 44.1 kHz. The full files were processed through the fixture CLI; the recording UI remains limited to 30 seconds. Scale Snap, Magnet and time quantization were off. The existing YIN pipeline was evaluated through the same CLI with its current UI pitch settings; it is a comparison mode only.

Attribution: Rachel Bittner, Katherine Pasalo, Juan José Bosch, Gabriel Meseguer Brocal and David Rubinstein (2021), *Vocadito: A dataset of solo vocals with f0, note, and lyric annotations*, DOI [10.5281/zenodo.5578807](https://doi.org/10.5281/zenodo.5578807), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Downloaded `vocadito.zip` MD5: `dea40fd18f14d899643c4ba221b33a46`. Source audio and annotations stayed local under ignored build storage and are not bundled or uploaded. Model provenance is in [MODEL.md](../resources/vocal/MODEL.md).

F1 uses maximum one-to-one note matching within 50 ms onset and 50 cents pitch. It does **not** require an offset match. Onset and duration MAE apply only to those matched notes, so they do not account for missed notes. Pitch MAE uses a separate maximum matching within 150 ms onset, without a pitch restriction, and is in semitones; it is not frame-level pitch accuracy. These custom pilot metrics are not a full published benchmark or a percentage of the performance reproduced correctly.

| Clip | Engine/preset | Predicted / reference notes | Note F1 | Onset MAE (ms) | Duration MAE (ms) | Pitch MAE (semitones) | Processing (s) |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | Neural Natural | 80 / 59 | 60.4% | 19.2 | 50.8 | 0.48 | 2.15 |
| 1 | Neural Clean | 70 / 59 | 63.6% | 18.7 | 40.5 | 0.44 | 1.98 |
| 1 | Neural Tight | 66 / 59 | 62.4% | 20.9 | 45.7 | 0.44 | 1.99 |
| 1 | Existing YIN | 62 / 59 | 49.6% | 21.9 | 38.1 | 0.63 | 0.89 |
| 2 | Neural Natural | 70 / 76 | 56.2% | 15.2 | 146.0 | 0.60 | 2.24 |
| 2 | Neural Clean | 60 / 76 | 54.4% | 14.8 | 147.6 | 0.58 | 2.49 |
| 2 | Neural Tight | 53 / 76 | 54.3% | 14.7 | 180.7 | 0.60 | 2.46 |
| 2 | Existing YIN | 46 / 76 | 44.3% | 25.3 | 182.0 | 1.09 | 0.92 |
| 3 | Neural Natural | 52 / 51 | 66.0% | 10.1 | 40.7 | 0.31 | 1.62 |
| 3 | Neural Clean | 48 / 51 | 70.7% | 11.4 | 43.5 | 0.34 | 1.49 |
| 3 | Neural Tight | 46 / 51 | 57.7% | 11.2 | 58.1 | 0.35 | 1.40 |
| 3 | Existing YIN | 36 / 51 | 52.9% | 13.0 | 123.4 | 0.28 | 0.60 |

Processing times are individual final runs on this Mac, after competing builds completed, including resampling/model setup/conversion and excluding WAV loading. They are not latency guarantees. An earlier run during a concurrent build took up to 5.53 seconds for one clip. The old YIN pipeline is faster in this pilot. Exact final metrics and note CSVs are in `build/sing-midi-evaluation/serial/`.

Reproduction command actually executed after downloading/unpacking the dataset locally:

```sh
python3 scripts/evaluate-vocal.py \
  --dataset build/sing-midi-evaluation \
  --executable build/macos-intel/bin/HumVocalFixture \
  --output build/sing-midi-evaluation/serial
```

The dataset directory must contain `Audio/vocadito_{1,2,3}.wav` and `Annotations/Notes/vocadito_{1,2,3}_notesA1.csv`. The evaluator uses only Python's standard library and the local executable; it performs no downloads or uploads.

## Known failures and unverified behavior

The new engine has higher note F1 on these three clips, but the sample is too small to establish general superiority. Clip 1 still produces extra fragments, especially with Natural. Clip 2 misses articulations and has larger duration errors; stronger smoothing reduces note recall. Tight loses short changes on clip 3. The existing engine has lower matched duration error on clip 1 and lower onset-matched pitch error on clip 3. A neural pitch detector alone does not guarantee faithful note boundaries.

Live singing through the user's physical microphone and subjective similarity to their own humming remain unverified. Permission/device failures were tested with injected capture; native UI rendering and output startup were exercised separately. No claim of perfect transcription, accurate vocal dynamics, timbre reproduction, polyphonic/chord recognition, streaming conversion or pitch-bend export is made. All notes currently use velocity 96.

The new runtime requires macOS 13.3 or newer and was tested on Intel macOS 13.7.8. It loads only during conversion. The original engine retains its 13.0 app minimum, but startup on an actual 13.0 machine was not retested. Other operating systems and native runtimes were not packaged or validated here.

## Packaged app

Executed `PIP_NO_INDEX=1 LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh`. The build and all 26 tests passed again. Deployment verified Intel slices and absence of developer Qt/build paths in Mach-O dependencies. Local ad-hoc signing and `codesign --verify --deep --strict` passed. With developer Qt/DYLD variables unset, the deployed Cocoa app passed `--smoke-test` and `--vocal-backend-check`; the latter performed actual inference using the embedded model and the deployed runtime. Log: `build/sing-midi-package.log`.

Outputs:

- App: `build/macos-intel/packages/HumToSing.app` (about 120 MiB on disk).
- ZIP: `build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip`, 49,065,005 bytes (46.79 MiB).
- ZIP SHA-256: `3c8b72f24be1f85ce4c3b5b5c3a4f269c58d941897116c00e5b2c1546d286866`, independently checked with `shasum -a 256 -c` against its sidecar.
- Bundled signed native runtime: 28,842,560 bytes. Model and runtime license notices are included in the app's Documentation/Vocal directory.

This is a local evaluation package with ad-hoc signing, not an Apple-notarized public release. The checks validate this packaged build; they do not substitute for trying the user's actual voice and microphone.
