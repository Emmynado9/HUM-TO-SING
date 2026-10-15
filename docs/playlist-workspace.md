# Playlist and floating screens

The Playlist is the main workspace. It uses the existing black, white, light-gray and muted sage palette. The top **Hum → MIDI**, **Piano roll**, and **Pitch Lab** buttons open reusable, non-modal desktop windows above it. Closing a feature window hides that view; it does not discard notes or stop an active recording. While recording, the top button reads **Recording… open controls**. Reopen it to Stop; the existing 30-second capture limit still applies. Closing the application runs the existing audio/worker cleanup.

## Arrange a sketch

1. Choose **+ Track** to add lanes (up to 16). Click a lane to select where the next recorded phrase will go.
2. Open **Hum → MIDI**, record one monophonic phrase, and Stop. A successful conversion appends a new region after the last region on that lane. Existing regions are retained. Empty, failed, or cancelled conversions leave the arrangement intact.
3. Drag a region left/right to change its placement, or up/down to move it to another lane. Relative note timing, pitches, durations, velocities and MIDI channels are preserved. The drag preview changes no project data; releasing commits one undo operation. Escape cancels it. Movement cannot create negative start times.
4. Select a region and choose **Duplicate region** (Ctrl+D) or **Delete region** (Delete). A duplicate starts immediately after its source. The copies contain independent editable canonical notes; changing one does not edit the other. Undo/redo covers region moves, duplicates, deletion and track addition, alongside existing note edits.
5. Double-click a region or choose **Piano roll** to correct notes. This screen shows the arrangement's canonical note events at their absolute times. Editing does not rerun transcription. Adding a note puts it in the selected note's region, or in the last region when no note is selected; an empty project creates its first region automatically.
6. Use the existing key/scale, Magnet, tempo, rhythm-snap and zoom controls in the piano-roll window. Ctrl+wheel also zooms the Playlist; Shift+wheel scrolls time and ordinary wheel scrolls lanes. The 4/4 bar ruler is a guide. Tempo changes never stretch the arrangement. Rhythm snapping is explicitly optional and defaults OFF; an enabled snap rounds the region's first-note onset, preserving internal timing. Magnet affects derived pitch only.
7. **Play melody** auditions the entire arrangement with the existing built-in soft tone; **Export MIDI** exports those same projected pitches and arranged times. The playhead still follows the audio output clock. Each lane uses the same instrument; independent instruments, mute/solo, mixing and per-track MIDI routing are not implemented. MIDI remains format 0 with one merged track, retaining canonical note channels. Lanes and region boundaries are retained by `.humproj`, not by this MIDI export.
8. **Save project** stores canonical notes, region membership, lane placement/count and existing musical settings. Version 2 project files remain local and use atomic safe writes. Version 1 files load as one region on Track 1; they are not modified until explicitly saved.

Original microphone audio and raw pitch frames remain session-only for the most recent take. New recording replaces that audio, while previously converted regions remain as notes. No recording is embedded in a project or uploaded. Existing bounded synthesis/rendering limits still apply to long arrangements; multiple lanes do not remove those resource bounds.

Overlaps between regions and notes are preserved as independent events. Already-authored overlapping MIDI notes can be arranged together; this does not add chord recognition or polyphonic vocal transcription. The user's 2026-10-14 request explicitly authorizes multiple arrangement lanes; other deferred roadmap features remain deferred.

## Detector choice

This implementation uses `YinPitchDetector` behind `IPitchDetector` for both live diagnostics and offline monophonic phrase conversion. Spotify Basic Pitch is not installed or invoked. The earlier dependency review records its Apache-2.0 project license and file-based neural transcription workflow. It is a candidate for a separate offline conversion evaluation, with an exact release/model/runtime license review and human-voice comparison before adoption. No detector, dependency or model change is part of the Playlist work.

## Verification

Automated tests cover canonical membership remapping, region transactions and undo/redo, invalid timing/lane/reference rejection, bounded lane count, legacy project loading, version 2 round trips, arranged MIDI timing/pitches, offscreen drag previews, cross-lane gestures, optional snapping and popup opening/hiding/reopening. The cloud host has no live microphone, audio output or native desktop session.

Manual desktop check: launch; add three lanes; select Track 2; open Hum → MIDI; record/Stop; close that window; drag the region to Track 1 and later in time; duplicate it; edit a note in Piano roll; Undo/Redo; toggle Magnet; Play/Stop; save/reopen; confirm the three lanes and all edits; export/import MIDI in a DAW. Also close/reopen each feature window while a recording is active, verify the top recording indicator, then Stop and confirm original-audio audition. Mark native playback, recording and window-manager checks as unverified until this is actually executed.

Executed on Debian 13 x86_64, GCC 14.2 and Qt 6.8.2 with the offscreen platform (2026-10-14):

| Check | Result |
| --- | --- |
| Core configure/build/tests | PASS, 10/10 tests |
| Debug desktop configure/build/tests | PASS, 14/14 tests |
| Release desktop configure/build/tests | PASS, 14/14 tests |
| Real `MainWindow` diagnostic fixture | PASS: loaded and safely saved 3 lanes, 3 regions, 16 authored canonical notes, including overlapping notes; exact note/region/lane round trip; opened all three floating screens |
| Native microphone, audible output, window-manager placement and DAW import | NOT RUN: no available audio device or native desktop session |

Commands executed from `/workspace/HUM-TO-SING` (each configure, build and test command exited 0):

```sh
/workspace/.hum-tools/cmake/data/bin/cmake --preset core
/workspace/.hum-tools/cmake/data/bin/cmake --build --preset core
/workspace/.hum-tools/cmake/data/bin/ctest --preset core
/workspace/.hum-tools/cmake/data/bin/cmake --preset debug -DCMAKE_PREFIX_PATH=/workspace/.hum-qt/usr
/workspace/.hum-tools/cmake/data/bin/cmake --build --preset debug
/workspace/.hum-tools/cmake/data/bin/ctest --preset debug
/workspace/.hum-tools/cmake/data/bin/cmake --preset release -DCMAKE_PREFIX_PATH=/workspace/.hum-qt/usr
/workspace/.hum-tools/cmake/data/bin/cmake --build --preset release
/workspace/.hum-tools/cmake/data/bin/ctest --preset release
git diff --check
```

Run `./build/debug/bin/HumToSing` or `./build/release/bin/HumToSing` in a native desktop session. CTest supplies `QT_QPA_PLATFORM=offscreen` for automated UI checks here. The separate local diagnostic harness is `/tmp/hum_arrangement_preview.cpp`; it uses authored MIDI fixtures, not a microphone recording. Generated screenshots in `build/` are ignored build artifacts. Existing uncommitted files, including the AUD-001 piano-roll fix and regression, were preserved. No dependencies or pitch-analysis code changed, and no new installer package was generated for this UI change.
