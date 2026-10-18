# Hum-to-MIDI Music Sketchpad

This repository contains the tested foundation of a local-first desktop application for songwriters who can hum ideas but may not play an instrument. The current recording, phrase-conversion, and piano-roll screen makes the intended workflow clear:

**Hum → Edit → Play → Export**

The Playlist now hosts multiple arrangement lanes with floating Hum → MIDI and Piano roll windows. The selected Playlist track is the only editable track in the piano roll; **Show other tracks** adds grey read-only references. Playlist controls change track type/instrument, mute, and delete with Undo. The piano roll includes group selection, Paint, Resize, Erase, Mute, Slice and Zoom tools. Playback offers pause/resume, seeking from either grid or ruler and an optional local metronome. Hum → MIDI also provides the click during recording, with BPM and beat display. The Playlist shows the speaker output and offers Test speakers. Select a loop area in either view or choose Loop all to repeat the arrangement. See [workspace controls](docs/playlist-workspace.md), [sampled instruments and progressions](docs/instruments-and-progressions.md), and [latest Mac verification](docs/track-editing-and-sustain-2026-10-18.md).

The app enumerates microphones, records one bounded vocal phrase, displays input level and source metadata, and plays the original recording for verification. Pitch detection runs locally after capture. Short uncertain voiced sections and returning octave errors are repaired using surrounding evidence; true rests remain separate. Pitch Lab is no longer a separate screen.

After Stop, a worker analyzes the saved phrase, retains the raw pitch/voicing frames, derives conservative monophonic note events, and places an editable copy on a piano roll. A user can select, add, delete, move, and resize note blocks, then undo or redo each edit. Time and pitch zoom and both scroll directions are available. Optional rhythm snapping is off by default, so captured seconds remain unchanged unless the user chooses a timing interval.

The pitch guide covers all 12 root pitch classes with Major and Natural minor scales. Out-of-scale notes are muted, dashed, and labeled. **Magnet OFF** preserves canonical pitches. **Magnet ON** derives the nearest valid scale pitch for display and export without replacing the underlying pitch; switching it off reveals the canonical phrase again. Selecting a key does not transpose the melody, and rhythm snapping remains a separate time control.

**Play** renders the current visible/export projection through the local instrument selected for each track (Mac system sampled instruments, with a procedural synthesizer on other platforms) and feeds it to the local audio output. Note boundaries are scheduled at output sample positions, while the UI timer only redraws the audio-clock playhead. Edits stop the playback sink and retain the editing position; the next Play starts there. Explicit Stop returns to zero. Output changes and app closure also stop playback. The Mac sample bank is provided by the operating system; playback requires no download, API, or network resource. **Play original** remains a separate control for the captured voice.

MIDI export reads the same projected notes heard by **Play melody** and writes a local Standard MIDI File format 0. The selectable project/export tempo maps preserved seconds to 480 ticks per quarter with nearest-tick resolution; it does not retime or rhythm-quantize notes. MIDI contains note and tempo instructions, not the original voice or built-in instrument audio.

**Save project** atomically stores schema-versioned canonical notes, tempo, key/scale, Magnet state, and rhythm-snap setting. Project files explicitly omit original audio and raw detector frames. Loading clears any session recording so unrelated audio cannot be mistaken for project content. Open, new recording, and close protect unsaved changes. See [docs/project-files.md](docs/project-files.md).

Recordings are limited to 30 seconds. They are never uploaded, logged, or written to a temporary file. Starting a completed new take replaces the previous take; closing the application releases audio memory. Project and MIDI files are written only to paths the user chooses.

## Requirements

- A C++20 compiler
- CMake 3.25 or newer
- Qt 6.5 or newer, Core/Gui/Widgets/Concurrent/Multimedia development modules, plus Qt Test when `BUILD_TESTING=ON`

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

Create the Debian 13 x86_64 evaluation package and portable archive with:

```sh
cmake --build build/debug --target package
```

Install the generated Debian package with:

```sh
sudo apt install ./build/debug/humtosing_0.1.0_amd64.deb
HumToSing
```

The package dynamically links Debian's Qt runtime and declares those dependencies; it does not bundle Qt, FFmpeg, a model, or audio assets. The first dependency installation may require configured Debian repositories, but the installed application has no runtime cloud requirement. The package is unsigned and has not been exercised through a native installer UI on this cloud host. Windows and macOS packages are untested. Read the [quick start](docs/quick-start.md) and [manual beta script](docs/mvp-beta-checklist.md).

## Piano-roll controls

- Click a block to select it. Drag its body to change pitch and start time; drag its right edge to change duration.
- Double-click empty grid space or choose **Add note** to create a half-second note. Choose **Delete** or press Delete/Backspace to remove the selection.
- Use **Undo**/**Redo** or the platform-standard Ctrl/Cmd shortcuts. One completed drag is one history operation.
- Use the Time zoom and Pitch zoom sliders. The scrollbars reveal later times and the full MIDI pitch range.
- Leave **Rhythm snap** off to preserve captured timing. Enabling it rounds edited times to the displayed second interval; it never changes pitch.
- Choose a root and Major or Natural minor to highlight the scale. This selection supplies guidance and does not transpose notes.
- Toggle **Magnet ON** to show and export closest in-scale pitches. Toggle it off to return to canonical pitches. Equal-distance choices resolve downward; at MIDI 0, an unavailable lower candidate is skipped.
- Hover over a block for its note name, MIDI number, onset, duration, and editing instructions.
- Use **Play** for the built-in instrument and moving red playhead; Pause/Resume keeps the position and Stop returns to the start. Use **Play original** only to verify the captured voice.
- Set **Export tempo** before saving/exporting. Tempo changes MIDI tick mapping and metadata; it does not quantize or stretch the melody.
- **Save project** preserves editable notes and settings, but deliberately omits the session-only original audio.

Overlapping notes remain independent events and are never silently deleted. The status line reports preserved overlaps. Same-pitch overlaps can be ambiguous in some MIDI receivers, so correct them manually when they are unintended.

## Repository map

- `include/hum/core/` — UI-free audio, analysis, extraction, scale, MIDI, synthesis, project, playback, and export contracts.
- `src/core/` — tested recording state machine, YIN-style pitch diagnostic, note segmenter, transactional note editor, procedural preview renderer, versioned project codec, Standard MIDI File writer, fixture WAV reader, MIDI timing/model, and scale primitives.
- `src/app/` — Qt desktop shell, editable piano roll and playhead, worker-based analysis/conversion/synthesis, atomic project store, microphone adapter, and separate original/melody playback adapters.
- `src/tools/` — offline stored-WAV fixture replay utility.
- `tests/` — deterministic pitch fixtures, fake-device recording tests, note-editor tests, and offscreen piano-roll interaction and launch checks.
- `docs/` — baseline, architecture decisions, recording/pitch contracts, manual checklists, MIDI contract, dependency review, and acceptance checklists.

See [docs/pitch-lab.md](docs/pitch-lab.md) for detector behavior, [docs/note-extraction.md](docs/note-extraction.md) for segmentation and MIDI rules, [docs/piano-roll.md](docs/piano-roll.md) for the editor contract, [docs/scale-guidance.md](docs/scale-guidance.md) for exact Magnet semantics, and [docs/project-files.md](docs/project-files.md) for persistence. This build environment has no audio device or native display session, so use the [single-track beta script](docs/mvp-beta-checklist.md) before making hardware or DAW claims.

The repository does not yet declare a license for its own source. Choose one before distributing the application or accepting outside contributions. Dependency terms are recorded in [docs/dependencies-and-licenses.md](docs/dependencies-and-licenses.md).
