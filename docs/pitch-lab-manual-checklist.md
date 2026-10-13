# Manual human-voice Pitch Lab checklist

Synthetic tones verify deterministic frequency math and failure behavior; they do not prove accuracy on a human voice. Run this checklist on real microphone hardware and record the device, room, source distance, sample rate, frame/hop, gate, and pitch range.

1. Sustain A4 near a trusted 440 Hz reference. Confirm the display settles on A4, cents move around zero, raw and smoothed frequency remain plausible, and voicing quality is clearly labeled as a periodicity measure.
2. Repeat on comfortable low, middle, and high notes across the selected vocal range. Compare against a trusted tuner and record median cents error plus any octave mistakes.
3. Hum with a closed mouth, sing an open vowel, and use a deliberately breathy tone. Record reliable-frame rate and whether breath noise creates `No reliable pitch`, jitter, or octave errors.
4. Add slow and fast vibrato. Confirm raw frequency follows the movement, display smoothing does not freeze it, and silence immediately clears the displayed note.
5. Change volume from quiet to loud without changing pitch. Confirm pitch remains stable above the gate, the low-level signal is rejected below it, and adjusting the gate has an understandable effect.
6. Test room noise, fan noise, speech, and unpitched consonants. Count reliable false detections over a fixed interval; do not summarize this as noise rejection without the measured count.
7. Sing near the minimum and maximum configured range. Look specifically for octave doubling/halving. Repeat with a larger frame at low frequencies and a narrower range around the intended note.
8. Try short attacks followed by a sustained note. Record the time from audible onset to the first stable display and compare it with the documented window, worker, and UI delays.
9. Change frame/hop/range/gate while recording. Confirm the display resets safely, reports invalid combinations, capture remains uninterrupted, and the UI stays responsive.
10. Stop and choose Analyze saved take. Confirm replay does not block playback controls and its final diagnostic agrees with the live portion when the take ends on a sustained note.

If breathy-voice reliability or octave performance misses the product target, preserve these recordings locally with consent and use them as regression fixtures. The proposed first fix is a designed low-pass decimator plus harmonic/octave candidate scoring behind `IPitchDetector`; a heavier model should be evaluated only if measured failures justify it.
