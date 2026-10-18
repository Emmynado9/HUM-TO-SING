# Chord conversion and piano-roll transport repair

The Chord progression tab previously did not determine automatic conversion. Automatic conversion instead inspected the selected Playlist lane, which usually remained Melody even after adding a Chords lane. The explicit chord conversion button also required a separately selected chord lane. The key selector truncated accidental names and used a scale mode visible only in the piano roll. Meanwhile, a single Play widget was inserted into both layouts: Qt moved it from the piano roll to the Playlist.

The selected recording tab now determines conversion. A matching destination is selected or created; newly created tracks become selected. Selecting a Playlist track also selects its recording mode. Chord roots are expanded into diatonic triads using the shared song key and major/natural-minor setting. The key labels include accidentals. Conversion captures the scale before starting, and mode/key/destination controls cannot change during recording or conversion. Progress, errors and cancellation are visible on the chord tab. The misleading Basic Pitch wording has been removed: conversion uses the existing local YIN detector. No audio uploads or new dependencies were introduced.

The piano roll has separate Play/Resume, Pause and Stop buttons connected to the shared arrangement transport, with a position display and a Space shortcut. Holding Space does not repeatedly toggle playback. Playback remains the whole Playlist arrangement. Note edits retain the existing behavior of stopping playback so the next Play renders the edited notes.

Changed production files: `src/app/MainWindow.cpp`, `src/app/PlaylistWidget.hpp`, `src/app/PlaylistWidget.cpp`. Regression checks were extended in `src/app/main.cpp`, `tests/arrangement_tests.cpp` and `tests/mvp_workflow_tests.cpp`. The application smoke test now operates the real track-creation dialog instead of a special production bypass.

## Commands actually executed

Build/test commands used `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8`.

- `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2`: baseline and updated builds passed.
- `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure`: baseline and updated suites passed, 16/16. New checks cover every scale degree in all twelve major and natural-minor keys, root timing preservation, synthetic capture through YIN and chord rendering, actual track creation/selection, key synchronization, and separate piano transport controls.
- `python3 build/chord-playback-checks/run.py`: native Qt diagnostic passed. It loads a local C-major chord fixture, clicks piano Play/Pause/Stop, verifies pause holds the playhead, and sends Space to the focused piano roll to resume. The test started local speaker playback. Log and screenshots are in `build/chord-playback-checks/`.
- `PIP_NO_INDEX=1 LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh`: completed, including 16 passing suites, deployment checks, local signing and packaged Cocoa smoke test. Log: `build/chord-playback-package.log`.
- `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 shasum -a 256 -c build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip.sha256`: OK.

Updated app: `build/macos-intel/packages/HumToSing.app`. Quit any older running copy before reopening this bundle.

## Remaining manual verification

No live human humming or microphone capture was performed in this repair. Synthetic fixtures verify the conversion pipeline, but do not establish detection accuracy for a particular voice, microphone or noisy room. Select Chord progression, choose the song key/scale, hum one root per chord and stop; then open Piano roll to inspect and audition the resulting triads. Hardware playback state and clock behavior were verified, not subjective instrument realism.
