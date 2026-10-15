# Project files

Hum-to-MIDI project files use the `.humproj` extension and schema version 2. Version 1 files remain readable as one phrase on Track 1 and are upgraded only when explicitly saved. They are local, UTF-8 text documents with a fixed field order. The application saves them with `QSaveFile`, which writes a sibling temporary file and commits it as the destination only after all bytes are written.

Stored data:

- canonical editable notes: MIDI pitch, velocity, channel, start seconds, and duration seconds;
- tempo in BPM and 480 ticks per quarter by default;
- selected root and Major or Natural minor mode;
- Magnet ON/OFF;
- optional rhythm-snap setting;
- arrangement lane count and phrase regions, with their names, lane placement and indices into the canonical note vector.

The visible Magnet pitches are derived from canonical pitches after load and are not stored as a second melody. This prevents repeated key changes from compounding snap results.

The line `audio_storage omitted` is mandatory. Original microphone audio, raw detector frames, and rendered preview audio remain in process memory only and are not embedded or referenced. Loading a project clears any session recording, and **Play original** remains disabled until the user makes a new recording. Closing the app releases session audio. Saving a project therefore preserves the editable musical sketch, not the source recording.

The loader rejects malformed fields, invalid MIDI values, invalid timing, files over 16 MiB, unexpected trailing data, and unsupported schema versions. A failed load leaves the current project intact. A failed save leaves the previous destination intact where the platform supports the `QSaveFile` atomic replacement contract. Open and close prompt when editable project changes are unsaved. A new recording appends a converted region and retains existing notes and the current project path; only the latest original audio/raw analysis remain available in memory.

Tempo maps seconds to MIDI ticks:

```text
ticks = round(seconds × tempo_bpm × ticks_per_quarter / 60)
```

Changing tempo does not stretch notes or change playback timing. Rhythm snap remains a separate opt-in editing aid.

See [Playlist and floating screens](playlist-workspace.md) for arrangement interactions, lane limits and merged MIDI export behavior.
