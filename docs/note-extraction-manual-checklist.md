# Manual humming and MIDI conversion checklist

This cloud machine exposes no microphone/audio device. Run this checklist on real hardware; record the OS, device, sample rate/channels, room, frame/hop, gate/range, and application revision.

1. Record a phrase with leading and trailing silence and three clearly separated notes. Stop and confirm conversion progress moves without freezing the recorder UI.
2. Compare the piano-roll events with the phrase. Record missed/extra notes, pitch errors, and onset/offset differences against a waveform or trusted reference.
3. Hum two notes of the same pitch with a clear breath or pause. Confirm two events. Repeat with no audible break and confirm the limitation is understandable if one event results.
4. Sustain one note with slow and fast vibrato, a pitch scoop, and breathiness. Note any split, octave error, or empty result rather than treating it as expected accuracy.
5. Include a very short grace note, a quiet note near the gate, a consonant, room noise, and a long silent gap. Confirm false and omitted events are clearly inspectable.
6. Change the Pitch Lab gate/range/frame/hop, choose Convert again, and verify conversion uses the new copied settings without altering the original recording.
7. Cancel conversion on a long take. Confirm the original remains playable, no stale result appears later, and Convert again succeeds.
8. Close the app while converting. Confirm capture is closed and no audio or MIDI temporary file remains.
9. Set a known export tempo, export MIDI to a chosen location, and open it with an independent MIDI inspector. Check format 0, 480 TPQ, the selected tempo, note pitches, velocities, and approximate timing.
10. Optionally import the file into a named DAW and document the exact result. Until that is performed, do not claim DAW compatibility from structural tests alone.
