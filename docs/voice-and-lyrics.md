# Playlist voice and Lyrics

**Record voice** in the Playlist opens the microphone controls and starts the same four-beat count-in used for MIDI overdubs. The microphone is warmed during the count-in, but those samples are discarded. On the downbeat, existing MIDI and voice tracks play while the new take records. **Stop recording**, the recorder's **Stop** button, or the 30-second limit commits the actual audio as a voice waveform lane. Cancelling during count-in creates no clip. Hum → MIDI and neural Sing to MIDI remain separate transcription workflows.

Click the timeline to seek; voice audio follows the same play, pause, seek and loop transport as MIDI. Right-click a voice lane to mute/unmute or delete its take. Voice lanes do not become editable piano-roll notes. MIDI Undo/Redo continues to operate on MIDI edits; voice deletion currently has no undo. There is no audio time stretching, trimming or audio export in this milestone. MIDI export contains only MIDI notes.

**Lyrics** opens a floating plain-text notepad for verse and chorus writing. It stays available during playback and recording. Closing the notepad retains its text; editing marks the song project unsaved. Use **Save project** or **Cmd+S** inside Lyrics to save. App close/project open uses the existing unsaved-changes prompt. This is explicit project saving, not autosave.

Voice audio and lyrics are embedded locally in `.humproj` schema 6, with no external audio files, paid APIs or uploads. Voice is stored in mono PCM16 at its source sample rate. Existing schema 1–5 projects still open. See [project files](project-files.md) for bounds and migration details.

## Verification

The tests extend the existing project codec and full-workspace test suites:

- Unicode, multiline lyrics and escaped quotes/backslashes survive exact save/load; older projects default to blank lyrics/no voices; malformed PCM, excessive counts and oversized lyrics are rejected.
- Signed PCM16 values, clip origin and mute survive project round trips. Core mixing checks seek into a sustained voice, resampling, muted exclusion and bounded allocation.
- Full-workspace tests use injected microphone samples and a deterministic guide to verify four-beat capture, no count-in samples in a voice clip, no MIDI mutation, reopening lyrics/audio, subsequent voice backing, mute and delete.
- Native Cocoa checks exercise the same workspace. Native audio-output checks verify the guide clock and voice-only playback, pause, seek, resume and completion.

Commands used from the checkout (locale `en_US.UTF-8`):

```sh
build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2
build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure
build/macos-tools/venv/bin/cmake --build --preset core
build/macos-tools/venv/bin/ctest --preset core
QT_QPA_PLATFORM=cocoa build/macos-intel/bin/hum_overdub_ui_tests --native-workspace-check build/voice-count-in.png
QT_QPA_PLATFORM=cocoa build/macos-intel/bin/hum_overdub_ui_tests --native-guide-check
PIP_NO_INDEX=1 bash scripts/build-intel-mac.sh
```

Physical microphone recording, headphone leakage and device-specific input/output latency have not been measured. The automated microphone input is synthetic; native output checks verify functioning playback and transport, not subjective audio quality. Use headphones when overdubbing.
