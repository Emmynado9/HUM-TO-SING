# Single-track MVP release-readiness results

Measured on 2026-10-13 on Debian GNU/Linux 13.6 x86_64, Linux 6.18.44, GCC 14.2.0, CMake 3.31.10, Qt 6.8.2, and five visible AMD EPYC 9V74 virtual CPUs. The host has no `/dev/snd`, `DISPLAY`, or `WAYLAND_DISPLAY`.

## Automated results

- The fresh Release build passed all 12 CTest suites: 9 Qt-free core/integration suites and 3 Qt launch/UI/store suites. The offscreen smoke test checks that the window and Open, Save, tempo, and melody-playback controls exist.
- Fake-device recording tests pass 78 assertions covering state transitions, duplicate controls, metadata, 44.1/48/96 kHz, stereo-to-mono conversion, bounded capture, clipping, silence, a short take, permission denial, missing/disconnected devices, recovery, repeated sessions, explicit discard, resampling, and shutdown cleanup.
- Pitch fixtures pass 36 assertions. Steady synthetic tones from 82.4 to 987.8 Hz remain below 5.865 cents median error; fixed-seed noise produces 0/86 reliable frames; silence and low-level gated input produce no reliable note; amplitude change and vibrato remain covered.
- Note extraction passes 78 assertions for silence, short tones, sustained/repeated notes, vibrato, uncertain gaps, hysteresis, cancellation, timing bounds, multiple tempos, and an independent Standard MIDI File reader.
- Project, synth, scale, editor, MIDI, and offscreen piano-roll/playhead suites validate corrupt/unsupported files, exact round trips, atomic overwrite, sample-index scheduling, release-to-zero, visible/audible/exported pitch consistency, and undo/redo.
- The fake-hardware end-to-end test performs capture → conversion → edit → C Major → Magnet ON → synth render → versioned save/reopen → projected MIDI export. Its independent reader verifies format 0, 480 PPQ, 96 BPM, projected pitch, and paired note events.

For the 1.68-second 48 kHz mono end-to-end fixture, the final direct Release run retained 322,560 bytes of captured float PCM, converted in approximately 33 ms, rendered the instrument preview in approximately 2 ms, produced 595,200 bytes of stereo preview PCM, and reported about 3.5 MiB process maximum RSS. These timings describe generated tones on this host, not human-voice or hardware latency.

The offscreen 2,000-note piano-roll fixture painted in under 2 ms in the measured Release run. The 250 ms packaged-app smoke launch sampled approximately 49 MiB peak RSS. This short startup sample does not represent memory during a full 30-second capture; bounded 48 kHz stereo capture alone can retain about 11 MiB of float PCM, with analysis and preview buffers in addition.

## Package and offline checks

Release artifacts:

- `build/release/humtosing_0.1.0_amd64.deb`
- `build/release/HumToSing-0.1.0-Linux.tar.gz`

The DEB declares Debian 13 Qt runtime dependencies, installs `/usr/bin/HumToSing`, a desktop entry, and user documentation, and contains 0755 directories/executable plus 0644 documentation. The package does not bundle Qt, FFmpeg, models, or sound assets. Extracting the DEB and launching its installed binary with Qt's offscreen platform passed and resolved Qt from `/lib`, without a workspace RPATH.

An `strace -f -e trace=network` of that packaged smoke launch recorded no `AF_INET` or `AF_INET6` socket or connection. The only connection was an unsuccessful local `AF_UNIX` attempt to `/var/run/pulse/native`. A source audit found no Qt Network client, URL, curl, upload, or backend call. This establishes offline startup and local code paths; microphone and speaker operation while physically disconnected from a network remains a manual beta step.

## Hardware-dependent status

| Check | Status | Evidence |
|---|---|---|
| Microphone record/stop on physical hardware | Not run | No `/dev/snd` |
| Original-audio speaker audition | Not run | No audio output |
| Built-in instrument audio and audible stuck-note check | Not run | No audio output |
| Input/output hot unplug | Not run | No audio devices |
| Native desktop interaction and accessibility | Not run | No display session; Qt offscreen tests pass |
| Native `apt install` transaction/menu launch | Not run | DEB was inspected, extracted, and its extracted binary smoke-tested |
| DAW import | Not run | Ardour, REAPER, Qtractor, Rosegarden, Bitwig, and LMMS were not installed |
| Windows and macOS | Not run | Debian 13 is the only available platform |

## Known issues and risks

1. **High, beta validation:** live voice, permission UI, audio output, device changes, and native interaction are unverified on this host. Reproduce with steps 1–8 of `mvp-beta-checklist.md` on Debian hardware.
2. **High, public distribution:** the repository has no owner-selected license and the package is unsigned. Choose the project license, complete the Qt/FFmpeg notice review for the target distribution, and sign release artifacts before public distribution.
3. **Medium, transcription:** synthetic tones do not establish accuracy for breathiness, consonants, pitch scoops, room noise, or octave-ambiguous humming. Record each case in beta step 4 and preserve false/omitted notes for evaluation.
4. **Medium, repeated notes:** uninterrupted same-pitch humming usually remains one event because intent cannot be inferred without a pause or onset cue. Hum the same pitch twice without a break to reproduce; add a small pause or split it manually.
5. **Medium, project audio policy:** `.humproj` deliberately omits original audio and raw frames. Save, close, reopen, and observe that **Play original** is disabled while editable notes remain. The UI and file schema state this behavior.
6. **Low, preview sound:** the built-in instrument is a single procedural soft tone with no patch selection. This is adequate for pitch/timing evaluation and remains offline.
7. **Low, configure diagnostics:** Qt configuration reports missing optional XKB development metadata and Vulkan headers in this cloud image. Builds and all tests pass; the application does not use Vulkan.

No placeholder success message remains in the workflow. Hardware and DAW outcomes above remain explicitly unverified.
