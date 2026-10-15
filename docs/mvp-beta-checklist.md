# Single-track MVP manual beta script

Record the OS, application package, microphone, output device, and result for every step. Do not mark a hardware step passed from synthetic automation.

1. Install the Debian package on a clean Debian 13 x86_64 user account and launch it from the application menu and terminal.
2. Disconnect the network, relaunch, and confirm the UI, microphone list, conversion, built-in instrument, save/load, and export remain usable.
3. Deny microphone permission. Confirm the message explains where to enable it and that no recording starts. Grant access and retry.
4. Choose a non-default microphone. Record silence, a very short sound, a sustained note, a three-note phrase with pauses, a repeated same pitch with a pause, vibrato, breathy humming, room noise, and a deliberately clipped take. Record false notes, omissions, boundaries, octave errors, and UI stalls.
5. Disconnect the input during recording. Confirm the partial take is handled, the error is actionable, and a later recording can start.
6. Repeat Record/Stop ten times, let one take reach 30 seconds, cancel one conversion, and close once during recording and once during conversion. Confirm there is no freeze, stale playhead, temporary audio file, or retained device session.
7. Select C Major. Correct one note, Undo, Redo, toggle Magnet ON/OFF, change key, and confirm canonical pitches return when Magnet is off.
8. Play the original recording, then **Play melody**. Confirm they are clearly different controls. Stop, restart, switch the output device, and close while melody playback is active. Listen for stuck tones and watch playhead synchronization.
9. Set a non-default tempo. Save the project, close, reopen, and compare every canonical note, velocity, start, duration, key/scale, Magnet, rhythm snap, and tempo. Confirm **Play original** is unavailable because project audio is explicitly omitted.
10. With Magnet ON, export MIDI. Import it into an available DAW, verify tempo and note timing/pitches, and confirm it uses the DAW instrument rather than containing preview audio. Repeat with Magnet OFF.
11. Corrupt a copy of a project file and change another copy to schema version 99. Confirm both fail without replacing the open project. Test save to an unwritable destination and cancel an unsaved-changes prompt.

## Current cloud status

The cloud host has no `/dev/snd` and no native display session. All microphone, speaker, native window interaction, device-change, installer UI, accessibility, and DAW steps above are **not run** here. The offscreen UI and fake-hardware workflow are automated separately.
