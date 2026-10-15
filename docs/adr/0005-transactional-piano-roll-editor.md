# ADR 0005: Transactional piano-roll editor over the canonical note model

- Status: accepted
- Date: 2026-10-13

## Context

Songwriters need to correct conservative monophonic transcription without rerunning analysis or losing the original recording and detector evidence. The view needs reversible gestures, accurate time/pitch geometry, optional rhythmic timing snap, and predictable handling of overlaps. The UI must not invent another MIDI model.

## Decision

Add a Qt-free `NoteEditor` that owns the editable `TimedMidiNote` vector, selection, validation, and before/after command history. Add `PianoRollTransform` for reversible seconds/pitch-to-content coordinates. The Qt `PianoRollWidget` renders and edits that model directly. It may keep one temporary note preview during a drag, but it commits only the final valid note on pointer release.

```text
PhraseConversionResult
  ├── immutable recording and raw pitch frames
  └── original derived TimedMidiNote values
                 │ explicit load/re-transcribe
                 ▼
            NoteEditor
       canonical editable notes
       + selection + history
                 │
          PianoRollWidget
        geometry and gestures
                 │
                 ▼
       MidiTimeline → SMF export
```

Rhythm snap is a `RhythmSnap` time transform. It is disabled by default and only changes the start/end times involved in an edit. It has no key or scale knowledge. The editor rejects negative starts, nonpositive durations, pitches outside 0–127, zero velocity, and invalid channels through the existing `TimedMidiNote` contract.

Overlaps use a preserve policy: every valid event remains independent. The UI reports overlap pairs and never silently merges or removes notes. Boundary-touching notes use half-open time ranges and do not overlap. An interval sweep counts overlaps in O(n log n) time for responsive status updates on dense documents.

## Consequences

- One drag creates one undo entry, while live preview remains outside the model.
- Export reflects the current editor document. Editing never changes the saved audio, raw pitch frames, or original transcription result.
- History stores document snapshots. This is simple and reliable for short monophonic phrases but uses memory proportional to note count times edit count; a delta command representation can replace it if much larger documents become an actual use case.
- Same-pitch overlaps are preserved but may be ambiguous in receivers that represent note state by channel and pitch. The UI reports them for manual correction.
- Native pointer behavior and accessibility still require manual tests on supported desktop platforms; automated interaction evidence is currently Qt's offscreen backend on Debian.
