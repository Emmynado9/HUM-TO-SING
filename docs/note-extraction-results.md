# Note extraction measured results

Recorded on 2026-10-13 in a Debug build on Debian GNU/Linux 13.6 x86_64, Linux 6.18.44, GCC 14.2.0, CMake 3.31.10, and Qt 6.8.2.

## Deterministic phrase fixture

The fixture uses 48 kHz mono floating-point samples, 4096-sample analysis frames, 512-sample hops, a 0.010 RMS gate, and local sine tones with 8 ms fades. Its 1.69-second phrase is:

```text
200 ms silence
A4 360 ms
180 ms silence
C5 300 ms
180 ms silence
C5 270 ms
200 ms silence
```

The pipeline emitted MIDI pitches 69, 72, and 72 in order. Maximum measured onset error was **30.7 ms** and maximum measured offset error was **42.7 ms**. Both are within the declared tolerance of one window plus two hops: **106.7 ms** at 48 kHz. The final direct test run processed the fixture in **266.8 ms**, about 0.158 times the audio duration. This is offline processing runtime, not microphone or UI latency, and can vary with CPU load and compiler mode.

The same suite verified:

- silence creates no note events;
- a 15 ms tone does not survive the 80 ms minimum-duration rule;
- a 45-cent, 5 Hz synthetic vibrato remains one A4 event;
- a short two-frame uncertainty is bridged while a longer pause splits two A4 events;
- uninterrupted A4 remains one event;
- alternating ±55-cent fluctuation after a stable onset remains one event;
- a confirmed A4-to-C5 transition creates two ordered events;
- conversion cancellation before analysis produces no derived notes.

## MIDI validation

The tests convert a 0.5-second onset and 0.25-second duration at 60, 120, and 180 BPM with 480 TPQ, then round-trip the onset to 0.5 seconds. A separate test-only MIDI reader parses the writer's result and verifies format 0, one track, 480 division, a 500000-microsecond tempo event for 120 BPM, every note-on/off pair, expected tick durations, and note-off-before-note-on ordering for a repeated pitch on one tick. It also reads back the exported local file byte-for-byte and removes it.

The direct executable reported **78 passing assertions**. Final CTest/build totals are recorded in the task report after the full suite is rerun.

## Hardware and human-voice limits

`/dev/snd` is absent in this environment. No live microphone phrase, audio playback, human-voice boundary accuracy, file-dialog export, external MIDI tool, or DAW import was tested. Synthetic tones do not prove performance on breathy voice, consonants, pitch scoops, room noise, vibrato wider than the hysteresis threshold, or octave-ambiguous humming. Use `note-extraction-manual-checklist.md` before making those claims.
