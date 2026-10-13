# Manual microphone hardware checklist

The automated build environment had no `/dev/snd` device, so no live capture or playback result is claimed. Run these checks on each target OS with real input and output hardware.

1. Build and run the app using the commands in `README.md`.
2. Confirm every connected input appears once, the system default is labeled, a different input can be selected, and Refresh preserves the current selection when it still exists.
3. On macOS, allow the microphone prompt and confirm the app appears under Privacy & Security → Microphone. Deny it once and verify the screen explains how to recover. On Windows and Linux, verify equivalent system privacy/backend behavior.
4. Record a normal five-second hum. Confirm the duration advances without UI stalls, the meter follows the voice, Stop enables only while recording, and the final sample rate, channel count, and sample count are plausible.
5. Play the original. Confirm its duration and sound match the captured phrase and Stop playback works. Repeat with the hardware's available 44.1, 48, and 96 kHz modes where supported.
6. Record loudly enough to reach full scale. Confirm the clipping warning latches and clears for a later clean take.
7. Record silence and a take shorter than 150 ms. Confirm each gets a clear message and the app remains ready for another take.
8. Press Record/Stop repeatedly and double-click each control. Confirm only one session exists and every completed take can be played.
9. Disconnect the selected device while recording. Confirm recording stops, the partial take is safe if samples arrived, the message names the disconnection, and reconnect/refresh permits another session.
10. Let a take reach 30 seconds. Confirm it stops automatically, reports the safety limit, retains no samples beyond the bound, and remains playable.
11. Close the window during recording and during playback preparation/playback. Confirm the device is released promptly, no process remains, and no recording or temporary audio file is left behind.
