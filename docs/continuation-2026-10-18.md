# Loop and melody-stability continuation — 2026-10-18

The most recent checkpoint already contained selected-range looping, Loop all, shared icon transport controls, and melody-stability changes. Its package log recorded a completed build with 20 passing test suites. This continuation verified that checkpoint and completed its missing usage and handoff documentation; no production code, dependencies, or audio-processing behavior changed.

Updated files: `README.md`, `docs/playlist-workspace.md`, `docs/note-extraction.md`, and this record. Original versions of the three existing documents are preserved under `build/continuation-2026-10-18-backup`. This folder has no Git metadata, so no Git diff or commit is claimed.

## Existing behavior verified

- Selected loop areas synchronize between the Playlist and piano roll. The audio clock stays inside the range through repeated wraps.
- Pause holds the playhead; Continue resumes. Loop all replaces the selected area, and disabling looping synchronizes both controls.
- Loop selection and playback preserve saved canonical notes. Loop state is session-only and does not trim export.
- The existing automated melody-stability suite covers vibrato, short dropouts and octave errors at different hop sizes, intentional semitone/octave changes, retained rests and a synthetic audio fixture processed through local YIN.
- The packaged app starts with the native Cocoa platform. Its signature and ZIP checksum validate, and its executable code matches the current desktop build.

## Commands actually executed

Build and CTest commands used `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8`.

| Command | Observed result |
| --- | --- |
| `git status --short` | No Git repository metadata. |
| `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2` | Baseline and final builds passed. |
| `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure` | Baseline and final runs passed, 20/20 suites each. |
| `python3 build/loop-checks/run.py` | Passed against the current build using native Qt windows and local speaker playback. No microphone capture. |
| `shasum -a 256 -c build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip.sha256` | OK. |
| `/usr/bin/codesign --verify --deep --strict build/macos-intel/packages/HumToSing.app` | Passed. |
| `QT_QPA_PLATFORM=cocoa build/macos-intel/packages/HumToSing.app/Contents/MacOS/HumToSing --smoke-test` | Passed. |
| `/usr/bin/dwarfdump --uuid` on build and packaged executables | Matching Intel build UUIDs. |

A direct `cmp` of the executables reported differences. Deployment/signing alters the executable, so raw equality was not used as the code check. A local Python Mach-O section comparison subsequently confirmed identical `__TEXT,__text` bytes between the built and packaged executables. No repackaging was needed for these documentation-only edits.

Logs: `build/continuation-2026-10-18-build.log`, `build/continuation-2026-10-18-tests.log`, `build/continuation-2026-10-18-package-smoke.log`, and `build/loop-checks/continuation-2026-10-18.log`.

## Ready for manual trial

Open `build/macos-intel/packages/HumToSing.app`. Select a loop area, press Play, try Pause/Continue, then Loop all. The app is locally signed, not Apple-notarized.

Live microphone permission/capture, accuracy on human humming, subjective instrument sound, loop-boundary sound quality and DAW import were not verified in this continuation. Automated buffer and clock checks do not establish those listening outcomes. Basic Pitch remains deferred; existing local YIN, canonical notes and reversible Magnet are preserved.
