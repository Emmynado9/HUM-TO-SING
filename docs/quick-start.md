# Quick start for songwriters

## Install on the tested platform

The generated package targets Debian 13 x86_64. From the package directory:

```sh
sudo apt install ./humtosing_0.1.0_amd64.deb
HumToSing
```

The first install may need the configured Debian repositories to obtain Qt runtime dependencies. After installation, recording, analysis, playback, project files, and MIDI export require no account, paid API, model download, backend, or internet connection.

## Make a sketch

1. Connect a microphone and speakers or headphones.
2. Launch **Hum-to-MIDI Music Sketchpad**.
3. Open **Hum → MIDI** from the top button and choose the microphone. If asked, enable access in the operating-system privacy dialog.
4. Open **Piano roll** to choose the musical settings. Leave **Key: C**, **Scale: Major**, and **Magnet OFF** for the first take.
5. In **Hum → MIDI**, choose **Record**, hum one clear melody line, then choose **Stop**. Keep the phrase under 30 seconds.
6. Use **Play original** to check the captured voice. This is separate from note playback.
7. After conversion, close the recording window and double-click the Playlist region to open the piano roll. Drag a note to change its time or pitch, drag its right edge to change length, and use Undo if needed.
8. Toggle **Magnet ON** to hear and export the closest C-major pitches. Toggle it off to recover the canonical detected/manual pitches. Selecting a key does not transpose the melody.
9. Choose **Play melody**. The red playhead follows the audio output clock while the built-in offline soft-tone instrument plays.
10. Choose **Save project…** to retain editable notes and settings. The original vocal recording is session-only and is not saved in the project.
11. Set **Tempo** and choose **Export MIDI…**. MIDI contains note and tempo instructions, not the sound of the built-in instrument or your voice.

Leave **Rhythm snap** off to preserve captured timing. Turn it on only when you want later note edits rounded to the shown time interval.

If no notes appear, try a quieter room, move closer to the microphone without clipping, sustain vowels or a closed-mouth hum, and leave small pauses between repeated notes. Continuous same-pitch humming does not reliably reveal repeated-note intent.

For multiple phrase regions and lanes, see [the Playlist guide](playlist-workspace.md). All lanes currently use the same built-in instrument.
