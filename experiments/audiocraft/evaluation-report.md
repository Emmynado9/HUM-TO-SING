# AudioCraft trial status

**Decision: not evaluated. The app includes an experimental trial screen at the user's request; production generation integration is not yet recommended.**

## Automated evidence

The baseline core build and 16 core test suites passed before implementation. After implementation, the core build and 17 suites passed, including ten trial contract tests. The desktop build and all 29 desktop test suites also passed. These use temporary synthetic audio and projects; no user recordings are uploaded.

The trial checks cover explicit clip selection and byte-exact PCM excerpt export; project preservation and lyrics exclusion; all instrument/style templates; invalid, silent and truncated inputs; no-overwrite behavior; request validation; simulated GPU preflight failure; clean notebook syntax and embedded-runner consistency; nine-row evaluation thresholds; and actual native neural transcription of a synthetic 220-Hz tone into serialized MIDI with flute program routing and a reference WAV.

Commands executed from this checkout:

```sh
LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/cmake --build --preset core
LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/ctest --preset core
LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2
LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure
python3 experiments/audiocraft/evaluate.py experiments/audiocraft/evaluation-template.csv
```

Local logs are in ignored `build/generation-baseline-build.log`, `build/generation-baseline-tests.log`, `build/generation-test-build.log` and `build/generation-tests.log`, `build/generation-desktop-build.log` and `build/generation-desktop-tests.log`. The blank evaluation sheet correctly reports `not_evaluated` with zero completed cases.

## Desktop trial screen

The subsequent desktop trial screen passed the 29-suite baseline and the expanded 30-suite desktop checks. Native Cocoa checks also exercised the main workspace entry point and the trial dialog, including synthetic WAV conversion, separate previews, cancellation, invalid input, and undoable MIDI insertion. A native screenshot exposed a layout overlap; the dialog now scrolls on shorter windows and the corrected layout was inspected. These are synthetic and UI checks, not human listening results.

Additional commands executed:

```sh
QT_QPA_PLATFORM=cocoa LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-intel/bin/hum_ai_trial_ui_tests --native-screenshot
QT_QPA_PLATFORM=cocoa LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-intel/bin/hum_overdub_ui_tests --native-workspace-check build/ai-trial-count-in.png
```

Logs: `build/ai-trial-ui-baseline-tests.log`, `build/ai-trial-ui-tests.log`, `build/ai-trial-native-ui.log`, and `build/ai-trial-native-workspace.log`.

## Not yet executed

- Free Colab environment installation and actual GPU model loading/inference.
- Generation from user-selected humming recordings.
- Listening evaluation of solo-instrument isolation, melodic relevance and musical usefulness.
- Instrument-generated audio transcription accuracy and measured manual correction effort.
- Cloud generation latency, peak memory use and repeatability across runtime allocations.

The Intel Mac used for local checks has no compatible CUDA GPU. A suitable free Colab allocation and three explicitly selected human recordings are still required. An unexecuted notebook or synthetic passing tone test is not evidence that MusicGen satisfies the songwriting workflow.

## Human trial matrix

| Excerpt | Piano | Acoustic guitar | Flute |
|---|---|---|---|
| Sustained phrase | Not run | Not run | Not run |
| Repeated notes and rests | Not run | Not run | Not run |
| Pitch jumps | Not run | Not run | Not run |

Use the blank evaluation template; do not invent ratings. Recommend integration only after all nine cases are rated, with at least seven passing all audio criteria and at least seven requiring none/minor MIDI corrections. If generation or transcription fails this gate, report that failure and revise the approach.

## Direct AI Trial recorder checks

The direct microphone recorder uses its own capture session, with a ten-second limit and source playback. Baseline build and all 30 tests passed. Post-change checks cover short-take rejection, automatic stopping and selection, export without project mutation, disconnection recovery, permission denial and capture cancellation on close, using an injected synthetic microphone. Native Cocoa UI checks passed and the layout was inspected. Actual human microphone capture and cloud-generated instrument quality were not tested. Hum-to-MIDI source code and processing remain unchanged.

Commands executed: the desktop CMake build and CTest commands above, plus `QT_QPA_PLATFORM=cocoa LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-intel/bin/hum_ai_trial_ui_tests --native-screenshot`. Logs are `build/ai-recorder-baseline-build.log`, `build/ai-recorder-baseline-tests.log`, `build/ai-recorder-build.log`, `build/ai-recorder-tests.log`, and `build/ai-recorder-native.log`.

## Interactive Colab progress workflow

The user chose to retain free Colab notebook interaction rather than an app-controlled GPU service. The notebook now combines explicit upload, generation and result download, and provides a separate download-only retry. Model progress uses MusicGen’s callback; loading remains indeterminate. Download progress measures kernel-to-browser bytes and elapsed time, with a rate-based estimate of remaining time. A completed transfer does not prove the user finished saving the file to disk.

The baseline build and 30-suite desktop tests passed. Fourteen Python trial tests passed, including subprocess progress/failure/interruption, notebook embedded-source consistency, mocked kernel file transfer, and a Node simulation of successful and truncated browser downloads. Native Cocoa AI Trial checks passed. Commands executed include the desktop build and CTest commands above, `python3 tests/generation_trial_tests.py build/macos-intel/bin/HumGenerationTrial --neural`, and the native screenshot command above. Logs: `build/ai-cloud-baseline-tests.log`, `build/ai-cloud-contract-tests.log`, `build/ai-cloud-native.log`. Live Colab installation, GPU inference, browser/kernel compatibility and human listening remain unverified. No user audio was uploaded during implementation.
