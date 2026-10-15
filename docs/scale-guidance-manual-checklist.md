# Scale-guidance manual test checklist

Run this with a native desktop display. Record the commit, OS, Qt version, display scale, color theme, and MIDI inspection tool.

## Guidance

- Load or add C4, C#4, D4, D#4, E4, F4, and F#4 in C Major with Magnet OFF.
- Confirm C, D, E, and F use in-scale row cues. Confirm C#, D#, and F# look muted, use dashed outlines, and display `OUT` or `!` without relying on color.
- Hover each out-of-scale note and confirm the tooltip says `OUT OF SCALE` and names the note.
- Visit every root selector entry and both modes. Confirm the guidance text always says key selection does not transpose the melody.

## Reversibility

- Note exact pitches, starts, durations, and velocities with Magnet OFF.
- Turn Magnet ON in C Major. Confirm C# moves to C, D# to D, and F# to F; inspect tooltips for original and visible names.
- Turn Magnet OFF and confirm every original pitch returns with timing and velocity unchanged.
- Turn Magnet ON, switch among several keys and modes, then return to C Major. Confirm results match the first C-major view and do not drift.

## Editing and history

- With Magnet ON, drag a projected note to an out-of-scale row. Confirm it immediately appears at the nearest scale row and its tooltip names the newly chosen canonical pitch.
- Undo once and confirm the prior canonical and projected pitches return. Redo once and confirm both return to the edited state.
- Resize, add, and delete while Magnet is ON; verify each remains one undo action.
- Confirm the Rhythm snap control remains independent and Magnet changes no note timing by itself.

## Export

- Export once with Magnet OFF and once with Magnet ON. Inspect both files in an independent MIDI reader or DAW.
- Confirm OFF contains canonical pitches and ON contains exactly the pitches visible on the piano roll.
- Confirm start ticks, durations, velocities, channel, event count, and order are equal between the two exports.

Do not mark native visuals, accessibility, or DAW behavior as verified from the offscreen automated suite.
