# Scale-guidance measured results

Recorded on 2026-10-13 in a Debug build on Debian GNU/Linux 13.6 x86_64, GCC 14.2.0, CMake 3.31.10, and Qt 6.8.2.

`hum_scale_guidance_tests` passed **9,357 assertions**. The deterministic suite evaluates every MIDI pitch from 0 through 127 under every one of the 12 roots for both Major and Natural minor. It checks interval membership, in-range output, idempotence, lower-pitch ties, MIDI boundaries, preservation of time/duration/velocity/channel, key recomputation from canonical notes, Magnet ON/OFF restoration, Magnet-on edits, undo/redo under the active scale, and visible-versus-serialized-MIDI pitch consistency.

`hum_piano_roll_ui_tests` passed **41 assertions** on Qt's offscreen platform. Scale-specific checks cover in-scale and out-of-scale text guidance, original and projected tooltip pitches, screen-row changes for C Major and D Major, one-transaction editing with Magnet ON, and undo/redo reprojection. Its 2,000-note repaint took **1.22 ms** in this run.

The final Qt-free core run passed **6/6** CTest tests in **1.10 seconds**. The final desktop run passed **8/8** tests in **1.49 seconds**, including the offscreen launch and piano-roll interaction checks.

No native display, microphone, MIDI receiver, or DAW was available. Color appearance, display scaling, accessibility announcement quality, live record-to-guidance workflow, and external MIDI import remain manual checks rather than verified claims.
