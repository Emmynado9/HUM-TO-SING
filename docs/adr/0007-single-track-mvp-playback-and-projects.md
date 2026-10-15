# ADR 0007: Sample-clocked preview and versioned single-track projects

## Status

Accepted for the standalone single-track MVP.

## Context

The existing application could capture, transcribe, edit, guide pitches with Magnet, and export MIDI, but it could not audition the effective MIDI notes or reopen an editable session. Playback must follow an audio clock, stop every voice during lifecycle changes, work offline, and avoid a licensed sound asset. Persistence must retain the canonical phrase and musical settings without implying that session audio is stored.

## Decision

`SimpleSynthRenderer` is a Qt-free, deterministic preview instrument. It schedules every onset and offset at a calculated output sample index, renders a soft sine tone with a small second harmonic, applies short attack/release envelopes, and clamps the mix. `QtMidiPlayback` renders on a worker and sends the finished in-memory PCM buffer to `QAudioSink`. The sink schedules audio; the 33 ms UI timer only reads `processedUSecs()` and redraws the piano-roll playhead. Stop, restart, output-device change, note/projection change, and application shutdown all stop and destroy the sink and buffer. The release tail reaches zero. No soundfont, sample, model, or network resource is used.

`ProjectDocument` stores schema version 1, tempo, ticks per quarter, root, scale mode, Magnet state, rhythm-snap setting, and canonical `TimedMidiNote` values. Project files deliberately contain `audio_storage omitted`. They do not contain the original recording, raw pitch frames, or rendered instrument audio. `ProjectFileCodec` strictly validates the text format, rejects unknown versions and trailing content, and limits note count. The desktop store limits input size to 16 MiB and uses `QSaveFile` so a successful save atomically commits a complete replacement.

The editor derives its Magnet view after load. Playback and export both consume `NoteEditor::projectedNotes()`, so the notes heard and exported have the same pitches. Tempo changes only the Standard MIDI File tempo and seconds-to-ticks mapping; they do not retime or rhythm-quantize the seconds-based phrase.

## Consequences

- Instrument preview is sample scheduled and offline, but intentionally simple.
- Pre-rendering keeps the audio callback free of synthesis and allocation. A ten-minute timeline limit and a 16-million-interleaved-sample allocation cap bound preview memory; ordinary microphone takes remain capped at 30 seconds.
- Stopping playback cannot leave a live synthesized voice because there is no external MIDI receiver and the sink/buffer are destroyed.
- Saving a project never promises to retain the original recording. Loading clears any session recording to prevent auditioning unrelated audio.
- Schema version 1 is intentionally strict. Future fields require a new compatible migration or schema version.
- The Debian package depends on dynamically linked system Qt libraries and plugins. It does not bundle Qt, FFmpeg, a model, or a sound asset.
