# Scale-guidance milestone acceptance checklist

- [x] Existing scale model, canonical editor/history, piano roll, and export path inspected.
- [x] Baseline core and desktop builds/tests executed before editing.
- [x] Root selector covers all 12 pitch classes.
- [x] Major and Natural minor patterns implemented with extensible interval representation.
- [x] In-scale rows and out-of-scale notes use color plus text/line-style cues.
- [x] Magnet ON/OFF is plainly labeled and key selection is explained as non-transposing.
- [x] Projection changes only pitch and resolves equal-distance ties downward.
- [x] Canonical pitches survive Magnet toggles and key/mode changes.
- [x] Magnet-on edits, key changes, and undo/redo have documented deterministic behavior.
- [x] Piano roll and MIDI export consume the same projected pitches.
- [x] All roots/modes, MIDI edges, ties, idempotence, timing/velocity preservation, history, and export are tested.
- [x] Automatic key detection, transposition, chord inference, tracks, and arrangement remain out of scope.
- [ ] Native-display visual and accessibility checklist completed.
- [ ] Exported Magnet-on MIDI inspected in an independent DAW.
- [ ] Windows and macOS build and interaction checks completed.
