# Playlist and floating screens

The Playlist is the main workspace. It uses the existing black, white, light-gray and muted sage palette. The top **Hum → MIDI** and **Piano roll** buttons open reusable, non-modal desktop windows above it. Closing a feature window hides that view; it does not discard notes or stop an active recording. While recording, the top button reads **Recording… open controls**. Reopen it to Stop; the existing 30-second capture limit still applies. Closing the application runs the existing audio/worker cleanup.

## Arrange a sketch

1. Choose **+ Track** to add lanes (up to 16). Click a lane to select where the next recorded phrase will go.
2. Open **Hum → MIDI**, record one monophonic phrase, and Stop. A successful conversion appends a new region after the last region on that lane. Existing regions are retained. Empty, failed, or cancelled conversions leave the arrangement intact.
3. Drag a region left/right to change its placement, or up/down to move it to another lane. Relative note timing, pitches, durations, velocities and MIDI channels are preserved. The drag preview changes no project data; releasing commits one undo operation. Escape cancels it. Movement cannot create negative start times.
4. Select a region and choose **Duplicate region** (Ctrl+D) or **Delete region** (Delete). A duplicate starts immediately after its source. The copies contain independent editable canonical notes; changing one does not edit the other. Undo/redo covers region moves, duplicates, deletion and track addition, alongside existing note edits.
5. Double-click a region or choose **Piano roll** to correct notes. This screen edits only the selected track's canonical note events at their absolute times. **Show other tracks** overlays grey read-only reference notes. Editing does not rerun transcription. Adding a note always targets the selected track, creating its first region when necessary.
6. Use the existing key/scale, Magnet, tempo, rhythm-snap and zoom controls in the piano-roll window. Ctrl+wheel also zooms the Playlist; Shift+wheel scrolls time and ordinary wheel scrolls lanes. The 4/4 bar ruler is a guide. Tempo changes never stretch the arrangement. Rhythm snapping is explicitly optional and defaults OFF; an enabled snap rounds the region's first-note onset, preserving internal timing. Magnet affects derived pitch only.
7. **Play** auditions the entire arrangement using each track's selected local instrument; **Export MIDI** exports the same unmuted projected pitches and arranged times. The playhead follows the audio output clock. Track mute overlays individual note mute. Instrument selection is available on both the Playlist and piano roll; solo and mixing controls remain deferred. MIDI remains format 0 with one merged track, retaining canonical note channels. Lanes and region boundaries are retained by `.humproj`, not by this MIDI export.
8. **Save project** stores canonical notes, region membership, lane placement/count and existing musical settings. Version 5 project files remain local and use atomic safe writes. Version 1 files load as one region on Track 1; version 2 files keep their lane arrangement. Neither is modified until explicitly saved.

Original microphone audio and raw pitch frames remain session-only for the most recent take. New recording replaces that audio, while previously converted regions remain as notes. No recording is embedded in a project or uploaded. Existing bounded synthesis/rendering limits still apply to long arrangements; multiple lanes do not remove those resource bounds.

Overlaps between regions and notes are preserved as independent events. Already-authored overlapping MIDI notes can be arranged together; this does not add chord recognition or polyphonic vocal transcription. The user's 2026-10-14 request explicitly authorizes multiple arrangement lanes; other deferred roadmap features remain deferred.

## Selected-track controls — 2026-10-18

Click a track or region, then use the controls directly above the Playlist to choose its **Melody / Chords** type or instrument, **Mute track / Unmute track**, or **Delete track**. These changes support Undo/Redo and project saving. Changing type sets how subsequent recordings are converted; it preserves existing notes. Deleting a track removes only its regions and notes and shifts later lanes up. Deleting the last lane leaves an empty track ready for a new take.

The piano roll follows this selection. Paint, Select all, marquee, Move, Resize, Slice, Mute, Delete and Quantise operate on the active track only. **Show other tracks** is off initially; when enabled it draws other lanes as grey references that cannot be selected or edited. Playback still auditions the full unmuted arrangement.

Track mute silences both playback and MIDI export, without rewriting canonical note mute flags. Unmuting restores only notes that were not individually muted. MIDI routing is assigned before muted notes are filtered out. Export uses melodic General MIDI channels first; when all 16 lanes are used, the sixteenth occupies channel 10, which some MIDI receivers interpret as percussion.

Pitch Lab has been removed. Hum → MIDI retains the microphone meter, recording controls, original-audio playback and phrase conversion. New projects start with Magnet OFF; older projects keep their saved choice. See [current verification and transcription limits](track-editing-and-sustain-2026-10-18.md).

## Editing tools and transport — 2026-10-17

The piano-roll icon toolbar exposes Paint, Select / move, Resize, Erase, Mute, Slice, Zoom and Snap immediately above the grid. The selected tool stays highlighted. Snap is a separate toggle for rhythm timing; the Magnet control still governs pitch. Drag empty space to select several notes; Shift-click adds or removes a note from the selection, and Cmd/Ctrl+A selects all. Drag any selected note to move the group with one undo operation. Group movement stops at time zero and MIDI pitch limits. Escape cancels a move preview. Paint creates a note on click (one selected grid interval long when snap is enabled). Resize works from the note body. Mute toggles the selection; Slice divides selected notes crossing the clicked time and retains each half in its original region. Zoom clicks magnify time; Alt-click zooms out.

**Play** starts from the ruler cursor, **Pause** holds the audio position, **Resume** continues, and **Stop / return to start** resets to zero. Click either grid or ruler to set the start position or seek during playback/pause. Clicks on notes or regions also select an editing position; dragging still edits them. The displayed playback position uses the audio output clock. Edits, tempo changes, and changes to Magnet, key or metronome stop playback before the next render while retaining the cursor. Explicit Stop returns to zero.

Enable **Metronome** to add a locally synthesized 4/4 click. With an empty project, Play runs a repeating click; Stop ends it. In **Hum → MIDI**, switch **Metronome ON** and set BPM before Record to hear the click while humming. Its beat indicator follows the audio clock, and the click stops with recording, including the duration limit or a capture failure. Use headphones to keep the click out of the microphone. Metronome clicks are not part of MIDI export. Muted notes remain editable and saved, and are silent in both instrument playback and MIDI export.

Rhythm snap defaults OFF. Musical intervals from 1/4 through 1/64 notes follow project tempo for subsequent edits; changing tempo never moves existing notes. Loaded fixed-second intervals from older projects remain fixed. Schema 3 saves mute state, metronome and grid settings alongside the existing canonical notes and arrangement.

Latest loop and transport verification is recorded in [Mac continuation results for October 18](continuation-2026-10-18.md). Earlier playback and toolbar verification is recorded in [audio and toolbar repair results](audio-toolbar-fix-2026-10-17.md). Earlier results are in [Mac continuation results](continuation-2026-10-17.md). The historical cloud results below predate these tools.

## Loop selection and transport — 2026-10-18

The Playlist transport sits before **+ Track**; the piano-roll transport sits to the right of its editing tools. Both operate the same arrangement. Hover over an icon for its action. Play becomes **Continue playback** while paused; Space toggles playback/pause.

Choose **Select loop area**, then drag horizontally across either grid or ruler. The range follows the current rhythm-snap setting, accepts either drag direction, and appears in both views. Releasing a valid selection enables looping and returns to the editing tool. Press **Play** to hear the selected time range. Escape cancels a pending selection without changing the previous range.

The **Loop** toggle disables or re-enables repetition while retaining an explicitly selected area. **Loop all** clears that area and repeats from time zero to the last note's end. With an empty project and the metronome enabled, Loop all covers one four-beat bar. Changing the range or loop mode stops playback; press Play again. Pause/Continue retains the position inside the range. Playlist ruler seeking during a loop wraps the requested position into the active range.

Loop selection is session-only playback state: it does not edit canonical notes, create undo entries, trim MIDI export, or persist in project files. Opening a project clears it. Loop playback repeats the rendered audio within the selected boundaries, with a short edge fade; notes crossing a boundary are heard as the selected portion of that render.

The existing local YIN conversion now holds sustained notes through short dropouts, vibrato and brief octave errors. Confirmed pitch changes retain their first supporting transition time. See [segmentation defaults and limits](note-extraction.md) and [continuation verification](continuation-2026-10-18.md). Human-voice accuracy still requires a live humming trial.

## Detector choice

This implementation uses `YinPitchDetector` behind `IPitchDetector` for offline monophonic phrase conversion. Spotify Basic Pitch is not installed or invoked. The earlier dependency review records its Apache-2.0 project license and file-based neural transcription workflow. It is a candidate for a separate offline conversion evaluation, with an exact release/model/runtime license review and human-voice comparison before adoption. No detector, dependency or model change is part of the Playlist work.

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
