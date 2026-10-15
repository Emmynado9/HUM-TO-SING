# Piano-roll milestone acceptance checklist

- [x] Existing model/UI inspected and baseline core and desktop suites executed before changes.
- [x] View uses the existing `TimedMidiNote` model through a Qt-free `NoteEditor`.
- [x] Labeled MIDI pitch axis, seconds grid, blocks, empty state, note names, and tooltips implemented.
- [x] Selection, add, delete, drag move, drag resize, and validation implemented.
- [x] Horizontal/vertical scrolling and zoom implemented.
- [x] Add/delete/move/resize have undo/redo; one drag is one transaction.
- [x] Rhythm snap is explicit, optional, off by default, and separate from Magnet pitch guidance.
- [x] Overlaps are preserved and reported under a documented policy.
- [x] Editing does not call transcription or mutate original audio/raw conversion results.
- [x] Edited canonical notes feed MIDI conversion directly with Magnet OFF or through the reversible visible/export projection with Magnet ON.
- [x] Coordinate, edit, history, bounds, overlap, MIDI, dense, and offscreen UI tests pass.
- [x] Full desktop build and current 8/8 CTest suite pass on Debian 13 x86_64.
- [ ] Native display interaction and accessibility checked manually.
- [ ] Live microphone record-to-edit flow checked with audio hardware.
- [ ] Exported MIDI opened in an independent DAW.
- [ ] Windows and macOS build, interaction, and accessibility checks completed.
