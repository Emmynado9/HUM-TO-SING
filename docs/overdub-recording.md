# Recording with backing tracks and a four-beat count-in

Both **Hum → MIDI** and **Piano roll → Sing to MIDI** can record a new take while existing Playlist notes play. Select the destination lane and position the playhead, then open the recorder.

1. Press **Record**. If the arrangement is playing, it pauses at its current position. The app prepares a snapshot of the backing tracks and opens the microphone; input during preparation/count-in is discarded.
2. Listen to **1, 2, 3, 4** at the project BPM. Capture begins after the fourth beat has finished, not on the fourth click. The playhead stays at the recording origin during the count-in.
3. Sing as the backing starts. Existing unmuted melody and chord notes play through their selected instruments. The playhead now advances with the recording. Use headphones to keep backing and click audio out of the microphone.
4. Press **Stop**. Hum → MIDI converts and appends the take automatically. Sing to MIDI retains its separate **Convert → Preview → Insert** flow. The new notes land where capture began; existing notes and regions are retained. Inserting the region is one Undo operation.

The four-beat count-in is always audible, including when **Metronome OFF** is selected. The metronome setting controls whether the click continues during the take. Stop or close the recording controls during the count-in to cancel without creating a new take. Output or input failures stop the accompaniment and release the microphone. Permission is resolved before the count-in.

The original recorder's **Chords** workflow still converts a single hummed root line to chords using the selected key/scale. This update does not add polyphonic vocal recognition. Sing to MIDI remains a monophonic neural alternative.

Each take runs forward once, up to 30 seconds. Loop settings are retained for normal playback but are not applied during recording, so the new take has one unambiguous timeline position. Choose an origin at or before 570 seconds in the 600-second timeline. Tempo, arrangement edits and competing playback controls are locked during the take. Existing muted lanes remain muted, including a muted destination lane after insertion.

## Implementation and timing

`hum::core::OverdubPlan` snapshots canonical projected notes and instruments, crops them to the take window, and preserves sustained notes crossing the recording origin. It does not mutate project notes. `recordingGuide` combines exactly four count-in clicks, backing audio and an optional continuing click into one bounded output buffer, including silence after a short backing phrase ends.

`RecordingGuidePlayer` uses the output's processed-audio clock to switch from count-in to capture. Its 5-ms precise timer never starts capture based on the time Record was clicked or the duration of rendering. The [Qt audio-clock API](https://doc.qt.io/qt-6.8/qaudiosink.html#processedUSecs) reports processed audio rather than wall time spent preparing. `ArmedCapture` opens the hardware early and discards input until the capture callback is installed, avoiding a second hardware startup on the downbeat. The take's origin includes observed timer lateness.

This is not a calibrated, sample-accurate duplex recorder. Device input/output latency, callback-buffer boundaries and operating-system scheduling can still produce a small alignment offset. Physical microphone/headphone loopback latency has not been measured. The shared count-in/backing buffer itself has no render gap at the transition. No audio is uploaded, and no API or backend is required.

## Verification

Baseline before editing: the Intel release build succeeded and all 26 existing tests passed (`build/overdub-baseline-build.log`, `build/overdub-baseline-tests.log`). Tested on Intel macOS 13.7.8 with Qt 6.8.2; commands used `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8`.

Executed checks:

| Command | Result |
|---|---|
| `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2` and `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure` | Build succeeded; **28/28** tests passed. Logs: `build/overdub-release-build.log`, `build/overdub-release-tests.log`. |
| `build/macos-tools/venv/bin/cmake --build --preset core` and `build/macos-tools/venv/bin/ctest --preset core` | Qt-free build succeeded; **16/16** tests passed. Logs: `build/overdub-core-build.log`, `build/overdub-core-tests.log`. |
| `QT_QPA_PLATFORM=cocoa build/macos-intel/bin/hum_overdub_ui_tests --native-guide-check` | Real native audio output played the guide; all four beat phases preceded the one capture-start callback. Cancellation during preparation prevented a later callback. Log: `build/overdub-native-guide-final.log`. |
| `QT_QPA_PLATFORM=cocoa build/macos-intel/bin/hum_overdub_ui_tests --native-workspace-check build/overdub-count-in.png` | Actual arrangement playback was running before Record. Current-position capture, backing instruments/mutes, count-in cancellation, melody and chord-root conversion, placement, Undo and closing controls passed. Capture input and guide advancement in this workspace test were injected. Native count-in layout was inspected. Log: `build/overdub-native-workspace.log`. |
| `python3 build/track-workspace-checks/run.py` | All 13 existing native workspace checks passed. This local harness is an ignored build artifact; committed CTest suites cover the reproducible regression checks. Log: `build/overdub-native-legacy.log`. |

Core tests cover 20/60/123/400 BPM, exactly four clicks, the boundary after the complete fourth beat, optional continuing click, empty and short backing, muted notes, instrument routing, sustained notes crossing the origin, invalid ranges, microphone warmup without saved samples, no second hardware open on the downbeat, and disconnection/cancellation. The neural-panel test retains its actual inference, reversible correction, insertion and export checks, and now verifies count-in cancellation, discarded count-in input and output failure cleanup.

An initial full-workspace harness run timed out because its shared `primaryButton` selector clicked Export rather than Record. The test now identifies the Record control explicitly; the native and automated runs above passed. Physical microphone/headphone duplex latency and subjective alignment with the user's singing remain unverified.
