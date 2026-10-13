# Pitch Lab analysis contract

## Signal path

```text
microphone callback
  → bounded interleaved capture buffer
  → UI copies published frames and averages source channels to mono
  → Qt Concurrent worker
       → RMS input gate
       → box-average decimation
       → YIN-style raw frequency + periodicity quality
  → five-frame median display smoother
  → frequency / nearest note / cents / No reliable pitch
```

Pitch analysis never runs in the audio callback. The callback does not allocate, wait for a worker, access disk, update widgets, or load a model. If the worker is behind, live display drops old diagnostic windows rather than blocking capture.

## Configuration and reliability

The UI exposes frame size, hop size, RMS input gate, and minimum/maximum vocal frequency. Defaults are:

| Setting | Default |
|---|---:|
| Frame | 4,096 source frames |
| Hop | 512 source frames |
| RMS gate | 0.010 |
| Vocal range | 80–1,000 Hz |
| YIN threshold | 0.15 |
| Minimum voicing quality | 0.80 |
| Display median | 5 reliable frames |
| Tuning reference | A4 = 440 Hz |

The window must cover at least two periods at the selected minimum pitch after decimation. Invalid combinations display a settings message rather than an estimate. A raw frame is unreliable when it is below the gate, lacks sufficient periodicity, falls outside the selected range, or has invalid data. The UI then says **No reliable pitch** and leaves note/cents blank.

`voicing_quality` is `1 − d′(τ)`, where `d′` is YIN's cumulative-mean normalized difference at the selected period. It measures periodic agreement within the frame. It is deliberately labeled “Voicing quality,” not confidence or probability.

## Pitch and cents rules

For every reliable raw frequency `f`:

```text
MIDI pitch = 69 + 12 × log2(f / 440)
nearest MIDI note = round(MIDI pitch)
cents deviation = 100 × (MIDI pitch − nearest MIDI note)
```

The display smoother takes the median of up to five raw MIDI-pitch values and converts that median back to frequency. The original raw estimate remains available in `DisplayPitchEstimate::raw` and appears in the UI detail line. One unreliable frame clears the history; smoothing never holds an old note through silence.

## Latency

Detector window delay and UI latency are separate:

- At 48 kHz, the default 4,096-frame window needs 85.3 ms of audio; at 44.1 kHz it needs 92.9 ms, and at 96 kHz it needs 42.7 ms.
- At 48 kHz, the 512-frame hop requests a new estimate every 10.7 ms.
- The UI polls at 33 ms, so a completed worker result can wait up to roughly another 33 ms before scheduling/display. Worker execution time is measured and shown separately.
- The median smoother needs up to five reliable frames to reach its full window. It displays earlier reliable frames immediately, so that history adds stabilization rather than a mandatory five-hop startup delay.

This is algorithmic/display latency, not a measured microphone-driver round trip. No live audio device was available in the build environment.

## Stored fixture replay

`HumPitchFixture` loads a user-supplied PCM16 or float32 RIFF/WAVE file and runs the same `PitchAnalysisRunner` used by in-memory replay:

```sh
./build/debug/bin/HumPitchFixture fixture.wav 4096 512 0.01 80 1000
```

It prints one CSV row per analysis frame, including raw/display frequency, note, cents, voicing quality, RMS, and status. Fixture audio stays local. The loader caps input at 512 MiB and does not support compressed audio.
