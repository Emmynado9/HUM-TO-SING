# Piano-roll measured results

Recorded on 2026-10-13 in a Debug build on Debian GNU/Linux 13.6 x86_64, Linux 6.18.44, GCC 14.2.0, CMake 3.31.10, and Qt 6.8.2. Qt UI tests used `QT_QPA_PLATFORM=offscreen`; no native display server or audio device was available.

## Core editor

`hum_note_editor_tests` passed 103 assertions after the final dense-test addition. The suite covers reversible coordinates at 50/160/520 pixels per second and 10/20/34 pixels per semitone; selection; add, delete, move, and resize; start/duration/pitch boundaries; optional rhythm snap; preserved overlaps; redo-branch replacement; and one history transaction for an entire drag-style update.

The edited document was converted at 120 BPM and 480 PPQ. A corrected MIDI pitch 65 at 0.750 seconds for 0.250 seconds became tick 720 with duration 240. The serialized file contained the corrected note-on event, showing that export uses the editor document.

The 5,000-note Debug fixture loads the document, selects and moves one note transactionally, and calculates overlap status. The exact elapsed time is reported by the executable on each run because it varies by host. The acceptance threshold is one second.

## Offscreen UI

`hum_piano_roll_ui_tests` now passes 41 assertions. In addition to the original editing and navigation coverage, it verifies non-color in-scale/out-of-scale guidance, original-versus-Magnet tooltip text, C-major and D-major reprojection, Magnet-on drag semantics, and undo/redo projection consistency.

The scale-guidance verification run painted 2,000 notes in **1.22 ms** on Qt's offscreen backend in Debug. The earlier 5,000-note core load, move, and overlap-count fixture completed in **2.49 ms**. These values measure this test environment and do not predict native compositor performance or sustained frame rate.

The final scale-guidance desktop CTest run passed **8/8** tests in **1.49 seconds**: six core tests, one application launch smoke test, and one piano-roll UI test. CMake emitted its existing nonfatal missing XKB and Vulkan-header discovery warnings.

## Unverified behavior

There is no `/dev/snd` device or native display session in this cloud environment. Live record-to-convert-to-edit flow, microphone permissions, audio verification playback, native mouse/touchpad behavior, screen-reader announcements, file-dialog export, external MIDI playback/import, Windows, and macOS were not tested here. The automated results establish model behavior, serialization wiring, widget interaction on the offscreen Qt backend, and startup without claiming those hardware or platform outcomes.
