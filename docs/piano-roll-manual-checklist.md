# Piano-roll manual test checklist

Use this on each supported desktop OS with a native display and microphone. Record the application commit, Qt version, OS, input device, sample rate, display scale, and pointing device.

## Record and load

- Select a microphone, record a short monophonic melody, and stop.
- Confirm the original audio plays and the converted blocks match the phrase order approximately.
- Note the original conversion summary and raw-frame count.
- Move and resize several notes; confirm no conversion progress starts and original-audio playback is unchanged.
- Choose **Re-transcribe recording** only after noting the edits; confirm it explicitly replaces the editable copy.

## Editing

- Select blocks at the left edge, center, and right edge of the timeline.
- Drag a block left to time zero and vertically to MIDI 0 and 127; confirm it cannot pass the bounds.
- Drag a right edge toward its start; confirm duration remains positive.
- Add notes by double-click and by the button, then delete by button and keyboard.
- Make, undo, and redo each add/delete/move/resize operation. Confirm one drag needs one undo.
- Undo, then make a different edit; confirm the abandoned redo is no longer available.
- Create overlapping notes; confirm both remain and the status reports an overlap.

## Navigation and help

- Scroll to late times and through the entire pitch range.
- Exercise both zoom sliders and Ctrl/Shift wheel shortcuts where supported.
- Confirm pitch labels, time labels, blocks, selection, and resize handle remain aligned at minimum and maximum zoom.
- Hover over a note and check that its note name, MIDI number, onset, duration, and instructions are readable.
- Check the empty-state text after deleting all notes.
- Navigate controls with the keyboard and inspect screen-reader names and focus visibility.

## Timing and export

- With rhythm snap off, make a small horizontal edit and confirm the displayed time is not rounded.
- Enable each snap interval and confirm timing rounds while pitch does not change.
- Export after correcting a pitch, start, and duration. Inspect the file with an independent MIDI tool or DAW and compare all three values.
- Test an unintended same-pitch overlap in the target MIDI receiver and document its note-off behavior.

Do not mark live recording, native UI behavior, accessibility, or DAW import as verified from automated offscreen tests alone.
