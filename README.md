# Hum-to-MIDI Music Sketchpad

This repository contains the tested foundation of a local-first desktop application for songwriters who can hum ideas but may not play an instrument. The current recording and phrase-conversion screen makes the intended workflow clear:

**Hum → Edit → Play → Export**

The app enumerates microphones, records one bounded vocal phrase, displays input level and source metadata, and plays the original recording for verification. During capture, the Pitch Lab estimates a single sung note and displays smoothed frequency, nearest note, cents deviation, and a periodicity-based voicing-quality measure. Unreliable input is labeled **No reliable pitch**.

After Stop, a worker analyzes the saved phrase, retains the raw pitch/voicing frames, derives conservative monophonic note events, and shows pitch, onset, duration, and velocity in a table. A successful result can be exported as a local Standard MIDI File format 0. Original seconds are preserved; the current file uses 120 BPM and 480 ticks per quarter, with only nearest-tick rounding. Scale snapping, rhythmic quantization, piano-roll editing, synthesized MIDI playback, and chord recognition remain later work.

Recordings are limited to 30 seconds. They are never uploaded, logged, or written to a temporary file. Starting a completed new take replaces the previous take; closing the application releases the audio memory.
MIDI is written only to the path the user chooses in the export dialog.

## Requirements

- A C++20 compiler
- CMake 3.25 or newer
- Qt 6.5 or newer, Core/Gui/Widgets/Concurrent/Multimedia development modules

On Debian or Ubuntu, the normal installation is:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake qt6-base-dev qt6-multimedia-dev
```

The project has no service, account, paid API, or cloud-backend dependency.

## Build, test, and run

From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/bin/HumToSing
```

On a display-less Linux build machine, exercise application startup with:

```sh
QT_QPA_PLATFORM=offscreen ./build/debug/bin/HumToSing --smoke-test
```

Build and test the core without Qt or the GUI:

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
```

Replay a stored PCM 16-bit or IEEE-float 32-bit WAV fixture through the same detector and display-smoothing pipeline:

```sh
./build/debug/bin/HumPitchFixture path/to/fixture.wav
```

Optional positional settings are `frame-size hop-size gate-rms min-hz max-hz`; run with `--help` for the full form. Fixture analysis reads only the path supplied by the user and never uploads it.

Create the current CPack archive scaffold with:

```sh
cmake --build build/debug --target package
```

The generated archive is a build artifact, not yet a distribution-ready installer: bundling Qt, code signing, platform permissions, and installer testing are later release tasks.

## Repository map

- `include/hum/core/` — UI-free audio, analysis, extraction, scale, MIDI, playback, and export contracts.
- `src/core/` — tested recording state machine, YIN-style pitch diagnostic, note segmenter, Standard MIDI File writer, fixture WAV reader, MIDI timing/model, and scale primitives.
- `src/app/` — Qt desktop shell, worker-based Pitch Lab and phrase conversion, microphone adapter, and playback adapter.
- `src/tools/` — offline stored-WAV fixture replay utility.
- `tests/` — deterministic pitch fixtures, fake-device recording tests, and core assertions; the GUI launch check is registered by CMake.
- `docs/` — baseline, architecture decisions, recording/pitch contracts, manual checklists, MIDI contract, dependency review, and acceptance checklists.

See [docs/pitch-lab.md](docs/pitch-lab.md) for detector behavior and [docs/note-extraction.md](docs/note-extraction.md) for segmentation, timing, velocity, cancellation, and MIDI rules. This build environment has no audio device, so live voice and end-to-end export behavior remain subject to the [phrase conversion checklist](docs/note-extraction-manual-checklist.md), [manual humming checklist](docs/pitch-lab-manual-checklist.md), and [microphone hardware checklist](docs/microphone-hardware-checklist.md).

The repository does not yet declare a license for its own source. Choose one before distributing the application or accepting outside contributions. Dependency terms are recorded in [docs/dependencies-and-licenses.md](docs/dependencies-and-licenses.md).
