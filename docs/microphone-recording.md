# Microphone recording contract

## Scope

This milestone captures one monophonic vocal phrase and lets the user hear the original audio. It does not detect frequency, infer notes, create MIDI, quantize, or display a piano roll.

## Captured data

`AudioRecording` stores normalized `float` PCM in interleaved source-channel order together with the device's selected format:

- `sample_rate_hz`: frames captured per second; copied from the device format and never assumed to be 44.1 or 48 kHz.
- `channel_count`: samples in each frame.
- `sample_count`: number of interleaved channel samples retained.
- `frame_count = sample_count / channel_count`.
- `duration_seconds = frame_count / sample_rate_hz`.

The 30-second bound is converted to a whole-frame capacity before capture. Data past that capacity is discarded, a limit flag is raised, and Stop runs on the control thread. An input block with a changed sample rate/channel count or an incomplete frame is rejected as a device-format failure.

## Mono path for Pitch Lab analysis

Pitch Lab uses `copyMonoFrames()` during capture and `AudioRecording::monoSamples()` for completed-take replay. Each output sample is the arithmetic mean of all channel samples in the corresponding frame:

```text
mono[frame] = sum(input[frame, channel]) / channel_count
```

This preserves frame count, sample rate, and timing. It also makes opposing channels cancel, which is mathematically correct for averaging but can expose a phase-inverted input configuration; a later diagnostic may offer channel selection if hardware testing shows that need. No resampling occurs on the analysis path.

## Verification playback

The app asks the current output device for its preferred format. If that differs from the captured format, `convertedForPlayback()` explicitly:

1. computes the target frame count from source frames and the rate ratio;
2. linearly interpolates in time;
3. keeps channels when their counts match, or averages to mono and duplicates that mono signal into every target channel.

Playback preparation runs with Qt Concurrent so allocation and conversion do not block the UI or capture callback. This resampler exists only to verify a take and preserves duration within one output frame.

## Realtime and privacy boundaries

The audio callback performs fixed-size sample conversion, a bounded memory copy, a peak scan, and atomic flag/counter updates. It does not allocate, lock, access disk, analyze pitch, touch UI widgets, or make network calls. Stop, immutable snapshot creation, playback conversion, and all UI work run elsewhere.

Audio stays in memory. The app creates no recording or temporary audio file, uploads nothing, and logs no audio contents. Closing the window stops an active source and releases the recording. Starting and completing another take replaces the prior take.
