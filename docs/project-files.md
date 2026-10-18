# Project files

Hum-to-MIDI project files use the `.humproj` extension and schema version 5. Version 1 files remain readable as one phrase on Track 1; version 2 retains its arrangement lanes and regions, version 3 retains note mute/metronome settings, and version 4 retains track types and instruments. Versions 1–4 default track mute to OFF. Older files are upgraded only when explicitly saved. They are local, UTF-8 text documents with a fixed field order. The application saves them with `QSaveFile`, which writes a sibling temporary file and commits it as the destination only after all bytes are written.

Stored data:

- canonical editable notes: MIDI pitch, velocity, channel, start seconds, duration seconds, and a reversible mute flag;
- tempo in BPM and 480 ticks per quarter by default;
- selected root and Major or Natural minor mode;
- Magnet ON/OFF;
- optional rhythm-snap setting and musical grid division (1/4 through 1/64 notes);
- metronome ON/OFF;
- per-track type, instrument and mute;
- arrangement lane count and phrase regions, with their names, lane placement and indices into the canonical note vector.

The visible Magnet pitches are derived from canonical pitches after load and are not stored as a second melody. This prevents repeated key changes from compounding snap results.

The line `audio_storage omitted` is mandatory. Original microphone audio, raw detector frames, and rendered preview audio remain in process memory only and are not embedded or referenced. Loading a project clears any session recording, and **Play original** remains disabled until the user makes a new recording. Closing the app releases session audio. Saving a project therefore preserves the editable musical sketch, not the source recording.

The loader rejects malformed fields, invalid MIDI values, invalid timing, files over 16 MiB, unexpected trailing data, and unsupported schema versions. A failed load leaves the current project intact. A failed save leaves the previous destination intact where the platform supports the `QSaveFile` atomic replacement contract. Open and close prompt when editable project changes are unsaved. A new recording appends a converted region and retains existing notes and the current project path; only the latest original audio/raw analysis remain available in memory.

Tempo maps seconds to MIDI ticks:

```text
ticks = round(seconds × tempo_bpm × ticks_per_quarter / 60)
```

Changing tempo does not stretch notes or change their playback timing. It changes the metronome and the interval used for subsequent edits on a musical grid. Rhythm snap remains a separate opt-in editing aid. Legacy fixed-second snap intervals remain fixed when tempo changes. Loaded MIDI tick resolution is retained by both project save and MIDI export.

Muted notes retain their canonical pitch, timing and region membership, but produce no instrument audio or MIDI note events. Track mute is stored separately and silences every note on that lane without replacing those per-note flags. Unmuting a track leaves individually muted notes silent. The metronome is a playback aid and is never exported as MIDI notes. Older projects load with unmuted notes and metronome OFF.

See [Playlist and floating screens](playlist-workspace.md) for arrangement interactions, lane limits and merged MIDI export behavior.
