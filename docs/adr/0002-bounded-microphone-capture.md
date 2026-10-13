# ADR 0002: Bounded, in-memory microphone capture

- Status: accepted
- Date: 2026-10-10

## Context

The recording milestone needs dependable local capture, visible diagnostics, verification playback, deterministic cleanup, and tests that do not require physical hardware. Pitch detection is intentionally outside this milestone. Audio callbacks must remain safe under realtime constraints, and the source device's actual format must remain available for later analysis.

## Decision

`RecordingController` owns the UI-independent state machine and accepts an `IAudioCapture`. `QtAudioCapture` is the desktop adapter; tests substitute `FakeAudioCapture`. A session preallocates enough interleaved `float` storage for 30 seconds in the selected device's preferred sample rate and channel count. The callback only converts bounded Qt PCM chunks, copies samples, scans a peak, and updates atomics. The UI timer services limit/failure flags and performs Stop/snapshot work on the control thread.

```text
QAudioSource callback
  → fixed-size PCM conversion scratch
  → preallocated interleaved float buffer + peak atomics
  → RecordingController service on UI timer
  → immutable AudioRecording
        ├── original-audio playback preparation on worker
        └── published mono-frame copies for the Pitch Lab worker
```

The completed `AudioRecording` keeps original sample rate and channels. Analysis will request mono samples, calculated as the arithmetic mean of all channels for each frame. Capture is not resampled. Verification playback performs an explicit linear resample and channel remix only when the output device requires another format; output frame count is calculated from the source duration.

## State and ownership rules

The states are `idle → starting → recording → stopping → ready`, with explicit `error` and terminal `closed` states. Record is rejected while a session is starting, recording, stopping, or awaiting error acknowledgement. Stop is idempotent. A device failure can preserve a valid partial take, then the error can be acknowledged before retrying.

Audio lives only in application memory. A completed replacement take replaces the preceding shared recording after Stop. Shutdown stops the source, invalidates callbacks through weak ownership, releases active and completed buffers, and enters `closed`. No temporary file, content log, network request, paid API, or cloud service is used.

## Consequences

- The maximum memory allocation depends on the device format. Allocation failure is reported as an unsupported recording format instead of starting an unbounded session.
- Linear interpolation is sufficient for verification playback, but is not the quality choice for future pitch analysis or production rendering.
- Live microphone/backend behavior needs manual validation on each packaged OS. The core state, limits, formats, conversion, recovery, and cleanup can be tested deterministically with the fake source.
