# ADR 0001: C++ core with a Qt desktop shell

- Status: accepted for the foundation milestone
- Date: 2026-10-10

## Context

The application needs dependable desktop microphone access, entirely local analysis, a conventional songwriter workflow, independent tests, distributable desktop builds, and a core that can later run behind a VST3 host boundary. The repository had no existing stack to preserve.

## Decision

Use C++20 for a framework-independent `hum_core` static library and Qt 6 Widgets for the standalone shell. UI classes depend on the core; the core never depends on Qt. The microphone adapter implements `IAudioCapture` with Qt Multimedia's `QAudioSource`, while analysis, note extraction, scale processing, MIDI data, and export remain ordinary C++.

```text
Qt Widgets shell
      │
      ├── IAudioCapture ── Qt Multimedia adapter
      ├── original-audio playback ── Qt Multimedia adapter
      └── hum_core
            ├── IPitchDetector
            ├── INoteSegmenter
            ├── IScaleProcessor
            ├── MidiSequence / Project
            └── IMidiExporter
```

The public contracts separate microphone capture, pitch analysis, note segmentation, scale processing, MIDI representation, playback/export, and UI. Later milestones implemented capture, verification playback, local pitch analysis, post-recording note segmentation, and Standard MIDI File export through these boundaries.

## Options considered

| Stack | Microphone/offline path | Testing and packaging | Licensing and VST3 reuse | Outcome |
|---|---|---|---|---|
| C++20 + Qt 6 | `QAudioSource` provides system input; native C++ buffers can stay local | Mature desktop widgets, CMake targets, platform deployment tools; core tests need no Qt | Qt modules are commercial or LGPL/GPL. Dynamic LGPL use has distribution obligations. A Qt-free core can be linked into a future plugin wrapper. | Selected for reliable desktop behavior and a clean engine boundary. |
| C++20 + SDL 3 | SDL recording devices and streams are cross-platform | Good low-level testing and permissive zlib license, but no standard desktop widgets; a second UI dependency would be required | Easy engine reuse and commercial distribution | Rejected for this milestone because it adds UI/tooling work with weaker native desktop semantics. |
| JUCE 9 | Strong integrated audio/device, standalone, and plugin support | Good cross-platform audio workflow and CMake support | Current modules are AGPLv3 or covered by the commercial JUCE 9 agreement. Vendor pricing could not be verified in this environment. | Deferred to avoid committing the product to AGPL or an unverified commercial plan. |

## Consequences

- Core tests build without Qt by setting `HUM_BUILD_GUI=OFF`.
- The desktop target links Qt Core/Gui/Widgets/Concurrent and Qt Multimedia. The core target remains Qt-free.
- A future VST3 target should wrap `hum_core`; it must not pull Qt widgets, device ownership, or desktop application state into the audio processor.
- Platform packaging must comply with Qt's selected license, bundle notices and license text, and preserve LGPL relinking/reverse-engineering rights where applicable. This is a release gate, not legal advice.
- The capture callback avoids allocation, locks, file I/O, UI calls, and unbounded buffers. It converts bounded PCM chunks, copies them into preallocated storage, calculates peak level, and updates atomics. Snapshotting, resampling, playback preparation, and future analysis run outside that callback.
- Windows and macOS behavior remains untested.
