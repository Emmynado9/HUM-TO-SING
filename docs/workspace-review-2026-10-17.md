# Workspace selection and navigation review — 2026-10-17

The user requested a check and repair of remaining workspace issues. The existing source folder was used; it has no Git metadata. Original versions of the five edited source/header/test files are preserved under `build/workspace-review-backup`.

## Reproduced and fixed

- Selecting an off-grid Playlist region with Snap enabled used to quantize its onset, even without a drag. Selection now preserves timing and creates no document-change notification or undo entry.
- Clicking a piano-roll note could similarly snap its onset. Clicking its resize edge or using the Resize tool could change its duration. Both views now use Qt's drag-distance threshold to distinguish a click or small pointer movement from an edit, including gestures delivered without an intermediate mouse-move event. The piano-roll guard applies to the preview as well as release.
- Playlist Enter previously opened the piano roll with the previous canonical note selected. Enter and double-click now share region-opening logic that selects the chosen region's first note and lane before opening the editor.
- Focusing the piano roll previously returned to the start of the entire arrangement, leaving a selected late region offscreen. It now brings the selected note's time and projected pitch into view. With no selection, it retains the arrangement-wide starting view.

Changes are confined to `PlaylistWidget.cpp/.hpp`, `PianoRollWidget.cpp`, their two existing UI test files, and this record. Core note data, reversible Magnet, playback/export, local YIN, dependencies and audio capture are unchanged.

## Commands actually executed

Paths are relative to the source folder. CMake and CTest commands used `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8`.

| Command | Observed result |
| --- | --- |
| `git status --short` | No Git repository metadata. No Git diff or commit claimed. |
| `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2` | Baseline and final desktop builds passed. |
| `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure` | Baseline and final desktop suites passed, 16/16. |
| `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2 --target hum_playlist_ui_tests hum_piano_roll_ui_tests` | Initial added tests failed to compile because `TimedMidiNote` has no equality operator. Test comparisons were corrected to check timing fields; the rebuild passed. |
| `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure -R 'hum_(playlist\|piano_roll)_ui_tests'` | Before production fixes, regressions reproduced selection edits and wrong-region navigation: 15 piano-roll and four Playlist assertions failed. After fixes, both suites passed in the full run. |
| `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2 --target hum_piano_roll_ui_tests` followed by `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure -R '^hum_piano_roll_ui_tests$'` | The added late-note visibility regression failed before the focus repair and passed in the final full run. |

The existing tests also continue to cover actual move/resize gestures, rhythmic snapping, group transactions, Escape cancellation, undo/redo, Magnet projection, toolbar actions, persistence and MIDI/export behavior.

`python3 build/audio-toolbar-checks/run_workspace_check.py` also passed. It exercises the real Open/Save controls with an authored fixture, retaining 960 PPQ, lanes/regions, mute, metronome and grid settings. It checks tempo-linked snap without retiming notes, toolbar wiring, and finite/looping PCM buffer reads and seeks without starting audio. Output is retained in `build/workspace-review-checks/workspace.log`.

`PIP_NO_INDEX=1 LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh` completed successfully using the existing pinned dependencies. It rebuilt the app, passed all 16 suites, deployed Qt, checked Intel slices and bundle dependency paths, locally signed and verified the bundle, passed the packaged Cocoa smoke check, and refreshed the application and ZIP. Log: `build/workspace-review-checks/package.log`.

`shasum -a 256 -c build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip.sha256` reported **OK** (with a harmless local Perl locale fallback warning).

Open `build/macos-intel/packages/HumToSing.app` for this build. The matching archive is `build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip`. The bundle is locally ad-hoc signed, not Apple-notarized.

## Manual follow-up

In the rebuilt app, enable Snap and click a region or note that starts between grid lines: timing should stay unchanged. Drag it to make an intentional snapped edit. Click a note's right edge without dragging: its length should stay unchanged. Select a later Playlist region and press Enter: the editor should select and show that region's first note.

Hardware microphone capture, audible playback, physical pointer feel, native floating-window placement and DAW import were not manually revalidated in this review. Automated tests do not establish those outcomes.
