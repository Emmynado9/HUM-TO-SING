# Sustained melody and selected-track editing — 2026-10-18

This change implements the user's request for steadier hummed notes, track deletion/type/instrument controls, track mute, seeking from either editor, removal of Pitch Lab, and piano-roll editing limited to the selected track. It preserves local YIN analysis, canonical note ownership, reversible Magnet, undo, and existing project loading.

## Behavior

- **Transcription:** short audible but uncertain detector sections are joined only when neighboring pitches agree. Returning octave errors require stable evidence on both sides. True silence remains a rest. The converter accepts lower fundamentals (55 Hz) and quieter tails (0.004 RMS gate). Raw detector frames remain unchanged. New projects start with Magnet OFF; loaded projects retain their saved choice.
- **Track controls:** select a lane/region and change Melody/Chords type, instrument, Mute/Unmute or Delete directly above the Playlist. Type changes govern future conversions without rewriting existing notes. Deleting a lane removes its notes/regions and remaps later lanes; one Undo restores them. Deleting the last lane leaves an empty recording destination.
- **Seeking:** clicks in either grid or ruler, including note/region clicks, position the shared playhead. Active playback seeks; paused playback stays paused. Editing stops the rendered buffer and retains the cursor for the next Play. Explicit Stop resets to zero.
- **Piano roll:** the selected track is the only editable track. Show other tracks overlays grey references. Selection, marquee, group operations, keyboard deletion and adding notes respect the active track, including empty tracks and after Undo.
- **Pitch Lab:** removed the window, navigation entry and live diagnostic worker from the application. Offline phrase analysis and original recording playback remain available in Hum → MIDI.
- **Persistence/export:** schema 5 saves independent track mute flags. Versions 1–4 remain readable. Playback and MIDI export use the same projected mute state. Export assigns channels before removing muted notes, fixing stale canonical indices when some notes are silent.

## Verification

The baseline build and all 20 suites passed. Added regressions first reproduced the voiced sustain split and returning octave error; after the extraction correction all 20 suites passed. One intermediate smoke check failed because its new Add note lookup lacked a stable widget name; that was corrected and the subsequent full run passed.

The melody tests exercise synthetic voiced dropouts, 160-ms true rests, intentional semitone/octave changes, different hop sizes and a harmonic-rich fading hum through actual YIN. The latter produces one D3 note lasting about 2.84 seconds. This is a deterministic fixture, not a live human recording.

Core regressions cover track type/instrument/mute, deletion/remapping, Undo/Redo, last-lane behavior, schema-4 migration, schema-5 round trips, invalid mute flags, export with muted notes and all sixteen lanes. UI checks cover active-track isolation, reference-note protection, empty-track note creation and seeking. The application smoke check exercises the actual track controls and retained editing cursor.

The native diagnostic at `build/track-workspace-checks/run.py` passed: it opens/saves an authored three-track project through the UI, changes track settings, deletes/undoes a lane, checks saved note/track mutes, places the playhead, inserts/undoes notes, and plays from the selected position on the local output device. It also verifies paused seeking, retained position after edits, playback restart and explicit Stop. Screenshots are retained alongside the diagnostic; no microphone recording was captured or uploaded.

Commands actually executed (build/test commands use `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8`):

- `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2`
- `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure`
- `build/macos-intel/bin/hum_melody_stability_tests`
- `python3 build/track-workspace-checks/run.py`
- `git diff --check`

Baseline logs: `build/track-edit-baseline-build.log`, `build/track-edit-baseline-tests.log`. Regression logs: `build/track-regression-build.log`, `build/track-regression-tests.log`. Updated build/test logs: `build/track-edit-final-build.log`, `build/track-edit-final-tests.log`. Native check log: `build/track-workspace-checks/result.log`.

The Qt-free core build also passed all 11 suites. The final Intel Mac packaging run rebuilt the application, passed all 20 desktop suites, validated deployed architectures and library paths, verified the local signature, and passed the packaged application smoke check. The release ZIP checksum was verified successfully.

Additional commands actually executed:

- `build/macos-tools/venv/bin/cmake --build --preset core`
- `build/macos-tools/venv/bin/ctest --preset core`
- `PIP_NO_INDEX=1 LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh`
- `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 shasum -a 256 -c build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip.sha256`

Logs: `build/track-edit-core-build.log`, `build/track-edit-core-tests.log`, and `build/track-edit-package.log`. Updated app: `build/macos-intel/packages/HumToSing.app`. ZIP: `build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip`.

## Listening limits

Human humming accuracy, microphone behavior and subjective sound quality still need a fresh take in the updated app. The current MIDI model captures discrete note pitch/onset/duration; it does not reproduce vocal timbre, continuous pitch bends or every vibrato detail. Very fast intentional octave excursions of up to 280 ms may be held at the surrounding pitch by the repair heuristic. For faithful comparison, turn off Snap melody to key/Magnet in older projects and reconvert or record a new take; existing MIDI regions are not rewritten automatically.
