# Speaker playback, toolbars and recording metronome — 2026-10-17

## Reproduced cause and repair

The user confirmed that microphone recording works, but neither the original take nor MIDI could be heard. A local diagnostic reproduced the same rejection in both playback adapters: Qt reported `Built-in Output`, 44,100 Hz, two channels, with an **Unknown** preferred sample format. Both adapters rejected it before opening playback. The same device explicitly accepted Float PCM at that rate and channel count. The system output was not muted.

`AudioOutputFormat.hpp` now negotiates a supported PCM format when the preferred format is unusable. Both `QtAudioPlayback` and `QtMidiPlayback` use it. Valid preferred formats are preserved; bounded rate/channel alternatives are considered only when needed. Unsupported outputs still produce an explicit error. No speaker permission setting or system volume was changed.

A subsequent hardware-output diagnostic started both adapters, observed advancing output clocks with `NoError`, and observed normal completion. The original-audio fixture was a locally generated tone, not a microphone recording. This establishes functioning output streams; human confirmation of audibility and recognition of the user's melody is separate.

## UI and recording changes

- Removed the inert left-side icon labels. Working navigation, playback, export and arrangement icons sit above the Playlist.
- Added the requested eight piano-roll buttons directly above the grid: Paint, Select, Resize, Erase, Mute, Slice, Zoom and Snap. The chosen editing tool is highlighted. Snap independently toggles rhythmic time snapping and remembers the last selected interval; scale Magnet remains a separate pitch setting. Icons are drawn locally with Qt painting code.
- Added the current speaker output and **Test speakers** to the Playlist.
- Added **Metronome ON/OFF**, linked BPM and an audio-clock beat indicator to **Hum → MIDI**. The click starts after capture starts successfully and continues while humming. The recording-page toggle can stop/restart it during capture. Stop, the duration limit, capture failure and app shutdown stop the click. BPM is held during capture. A failed click reports an error while allowing the recording to continue.
- The click uses a separate playback instance and is not mixed into the saved microphone samples or MIDI export. Headphones prevent acoustic pickup by the microphone. Original recordings remain session-only, and no audio is uploaded.

Existing canonical notes, reversible Magnet, lane membership, MIDI export projection and local YIN processing remain in use. No new dependency was installed. The source folder has no Git metadata; the prior source, tests, documentation, CMake file and README are preserved under `build/audio-toolbar-backup`.

## Executed verification

Commands ran from the source folder. CMake/CTest use the project-local tool installation.

| Command/check | Observed result |
| --- | --- |
| `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2` before edits | Baseline build passed. |
| `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure` before edits | Baseline 14/14 suites passed despite the hardware format rejection. |
| Existing packaged app `--audio-diagnostics`; `system_profiler SPAudioDataType`; `osascript -e 'get volume settings'` | Built-in microphone and output found; system output unmuted. Read-only checks. |
| `python3 build/audio-toolbar-checks/run_audio_probe.py` | Before repair: both adapters rejected the Unknown preferred format. After repair: both used supported 44.1-kHz stereo Float PCM, advanced their clocks and completed normally. Logs: `baseline-audio.log`, `format-probe.log`, `repaired-audio.log` in that directory. |
| Updated desktop build and CTest | Passed, 16/16 suites. New regressions cover Unknown-format negotiation and the eight actionable icon buttons, selection highlighting, independent Snap and actual mute/export behavior. Smoke checks exercise linked recording BPM/metronome controls without capturing audio. |
| `python3 build/audio-toolbar-checks/run_workspace_check.py` | Real Open/Save retained 960 PPQ, three lanes/regions, mute, metronome and grid settings; toolbar selection changed the editor tool. Generated Playlist, piano-roll and recording-window screenshots were visually reviewed. |
| `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-intel/bin/HumToSing.app/Contents/MacOS/HumToSing --audio-playback-check` | Passed on the built-in output: negotiated format, advancing MIDI clock, pause holding position, seek while paused, resume, Stop, original-audio playback/completion, and metronome continuing beyond a bar before stopping. No microphone access. Log: `build/audio-toolbar-checks/hardware-playback.log`. |
| `PIP_NO_INDEX=1 LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh` | Builds/tests and refreshes the local app/ZIP, including Qt deployment, Intel architecture checks, local signing verification and native Cocoa smoke check. Log: `build/audio-toolbar-checks/package.log`. |

The explicit `--audio-playback-check` diagnostic plays short, quiet generated tones and a short click. It is not run by CTest or normal app launch.

## Manual follow-up

Launch the rebuilt app, choose **Test speakers**, then make a fresh take with the recording-page metronome ON. Verify that the click is audible while humming, stops with Stop, and that **Play original** and Playlist **Play** are audible. Compare the detected notes with the take. Microphone capture is user-confirmed; simultaneous capture/click, acoustic audibility, headphone routing and transcription accuracy for this melody still need this manual check. Automated checks do not claim those outcomes.
