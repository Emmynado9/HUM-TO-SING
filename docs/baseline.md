# Baseline record

Recorded on 2026-10-10 before implementation.

The repository was empty: its local branch was unborn, `HEAD` did not resolve, the remote advertised no branches or tags, and there were no tracked or untracked project files. Therefore there was no dependency manifest, build configuration, application, test suite, or baseline build/test command to execute. No pre-existing behavior or failure was available to preserve.

No file matching the attached “Hum-to-MIDI Music Sketchpad — Project Blueprint” was present under `/workspace`, `/workspace/shared`, `/workspace/library-files`, or the repository. This foundation follows the requirements provided in the chat request. It must be reconciled with the blueprint when that attachment becomes available; this record does not claim the blueprint was read.

The planned change set was stated before editing: project guardrails; CMake configuration; framework-independent core models and interfaces; Qt desktop shell; core and launch tests; setup, architecture, MIDI, license, and acceptance documentation.

## Microphone milestone baseline

The blueprint was supplied in the conversation on 2026-10-10 and was read before the microphone milestone continued. Its staged plan confirms that microphone capture and a diagnostic screen come before pitch detection. The requested monochrome dashboard reference was also used for the recording-screen visual direction.

Before microphone changes, the C++20/Qt 6 foundation configured and built successfully. `ctest --preset core` passed its one core test, and `ctest --preset debug` passed the core test plus the offscreen GUI smoke test. The shell launched, but its audio interfaces were placeholders: it could not enumerate, record, meter, retain, or play microphone input. Qt Multimedia was reviewed but was not yet linked.

The stated microphone change set covered `CMakeLists.txt`; the core audio and recording model; Qt capture, playback, meter, and main-window components; fake-source recording tests; and recording architecture, dependency, privacy, and hardware-checklist documentation.

## Pitch Lab milestone baseline

Before Pitch Lab changes on 2026-10-10, the Qt-free build completed and CTest passed 2/2 tests (`hum_core_tests` and `hum_recording_tests`). The Qt desktop build also completed and passed 3/3 tests, adding the offscreen app smoke check. CMake repeated the existing nonfatal XKB and Vulkan-header discovery warnings.

Microphone recording, source metadata, level/clipping diagnostics, and original-audio playback already worked in code and automated fake-source tests. There was no pitch-detector implementation, analysis worker, voicing decision, tuning display, stored-audio fixture runner, or pitch test suite. Live microphone behavior remained unverified because the cloud environment exposed no `/dev/snd` or PulseAudio service.

The Pitch Lab plan was stated before editing and was limited to a replaceable core detector, safe published-frame copies, an asynchronous UI processor, live/replay diagnostics, deterministic pitch fixtures, a local WAV fixture tool, and architecture/license/test documentation. Piano-roll editing and chord recognition were excluded.

## Note extraction milestone baseline

Before note extraction changes on 2026-10-13, the Qt-free configuration and build completed and `ctest --preset core` passed 3/3 tests: core, recording, and pitch. The desktop configuration and build completed and `ctest --preset debug` passed 4/4 tests, including the offscreen application smoke check. CMake repeated its existing nonfatal XKB and Vulkan-header discovery warnings.

Reusable behavior already included bounded in-memory capture, mono conversion, metadata, playback of the original recording, raw YIN pitch frames, display smoothing, worker execution, and MIDI validation/time conversion primitives. There was no seconds-based extracted-note model, note segmenter, conversion worker, conversion progress/cancellation UI, note-event table, Standard MIDI File writer, or MIDI structural test. Live hardware remained unavailable because this cloud environment exposed no audio device.

The planned files and components were stated before editing: raw pitch-runner access; seconds and tick MIDI models; new core extraction and file-writer components; a Qt phrase-conversion worker and main-window integration; deterministic extraction/MIDI tests; CMake registration; and focused README, ADR, timing, results, and acceptance documentation.

## Piano-roll milestone baseline

Before piano-roll changes on 2026-10-13, the Qt-free configuration and build completed and `ctest --preset core` passed 4/4 tests: core, recording, pitch, and note extraction. The desktop configuration and build completed and `ctest --preset debug` passed 5/5 tests, including the offscreen application smoke check. CMake repeated the existing nonfatal missing XKB and Vulkan-header discovery warnings.

Reusable behavior already included the canonical seconds-based `TimedMidiNote` model, raw and derived post-recording results, seconds-to-MIDI-ticks conversion, and Standard MIDI File export. The desktop showed extracted notes in a read-only table and exported the conversion result directly. It had no piano-roll coordinate model, transactional editor, selection, add/delete/move/resize gestures, zoom, scrolling, rhythm-snap control, or editor-specific UI test.

The planned files and components were stated before editing: a Qt-free `NoteEditor` and coordinate transform; a Qt `PianoRollWidget`; main-window conversion/export integration; CMake and deterministic core/UI tests; and focused README, ADR, model, results, manual, and acceptance documentation. Existing capture, analysis, and transcription behavior remained outside the edit path.

## Scale-guidance milestone baseline

Before scale-guidance changes on 2026-10-13, the Qt-free configuration and build completed and `ctest --preset core` passed 5/5 tests. The desktop configuration and build completed and `ctest --preset debug` passed 7/7 tests, including the offscreen application and piano-roll interaction checks. CMake repeated the existing nonfatal missing XKB and Vulkan-header discovery warnings.

Reusable behavior included a basic interval-based `ScaleDefinition` with C-major defaults and a lower-pitch equal-distance rule, plus canonical `TimedMidiNote` editing, transactional history, piano-roll geometry, and export from canonical notes. The scale model was not connected to the editor or UI. There were no 12-root or mode selectors, natural-minor factory, in/out-of-scale cues, Magnet control, reversible projection, or scale/export consistency tests.

The planned changes were stated before editing: extend `scale.hpp/.cpp`; add a derived projection to `NoteEditor`; render scale guidance in `PianoRollWidget`; add root, mode, and Magnet controls and projected export in `MainWindow`; add focused core/UI tests and CMake registration; and update the relevant architecture, behavior, result, and manual-test documents. Audio capture, detection, transcription, rhythm timing, tracks, chords, and arrangement remained outside the change.

## Single-track MVP completion baseline — 2026-10-13

Before this milestone, the preserved working tree built successfully. The core-only preset passed **6/6** tests and the Qt desktop preset passed **8/8** tests. The Qt configure step reported nonfatal missing optional XKB development metadata and Vulkan headers. The offscreen app startup test passed. The host was Debian 13 x86_64 on five visible AMD EPYC 9V74 virtual CPUs and had no `/dev/snd` or native display session.

Working behavior already included bounded local microphone capture, original-recording audition, live/replay YIN pitch diagnostics, conservative post-recording note extraction, a canonical transactional piano roll, reversible Major/Natural-minor Magnet guidance, and format-0 MIDI export from the visible pitch projection. Existing changes for piano-roll and scale milestones were uncommitted and were preserved.

Exact remaining first-milestone gaps were: no MIDI instrument playback, Play/Stop or synchronized playhead; fixed 120 BPM UI export; no versioned project file or safe project write; no unsaved-change handling; no end-to-end capture-to-reopen-to-export regression; and only an unspecified CPack archive scaffold. Hardware recording/playback, native UI behavior, offline relaunch, clean installation, and DAW import had not been run.

The announced change set was limited to a Qt-free procedural synth and project codec; Qt audio-clock playback and atomic project storage adapters; Save/Open, tempo, melody playback, lifecycle, dirty-state, and playhead UI wiring; focused core/Qt/end-to-end tests; Debian package metadata; and the corresponding architecture, quick-start, acceptance, and release records. Existing capture, detector, segmentation, editing, scale, and SMF algorithms were not rewritten.
