# AudioCraft trial status

**Decision: not evaluated. No recommendation to integrate AudioCraft into the app yet.**

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
