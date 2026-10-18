# Mac workspace continuation — 2026-10-17

Continued the unfinished editing-tool, transport and schema-3 work already present in the source folder. No Git metadata is present here. Pre-continuation source, headers, tests and documentation are preserved in `build/continuation-backup`; the earlier `build/improvements-backup` remains intact.

## Changes completed

- Connected the piano-roll tool selector and group editing to the existing canonical editor. Group movement clamps all selected notes at time/pitch boundaries; release completes marquee selection even without an intermediate mouse-move event. Single-note moves retain their existing undo label.
- Connected Play/Pause/Resume, Stop, Playlist ruler seeking and the local metronome. Looping PCM buffers continue advertising available data at loop boundaries, and seeking preserves pause state. Metronome state is explicitly labeled ON/OFF.
- Connected musical snap intervals from 1/4 to 1/64 notes to tempo. Existing notes keep their absolute timing, and imported fixed-second snap intervals remain fixed.
- Connected schema-3 mute, metronome and grid persistence; retained loaded MIDI tick resolution in UI save and export. Version-1 and version-2 migration checks use independent historical fixtures.
- Added regression coverage for group transactions/undo, mute/export agreement, slice membership, legacy project migration, schema-3 fields, silent muted synthesis, metronome timing, editing tools and ruler seeking. The application smoke check also exercises tool, tempo/snap and metronome controls without starting audio.
- Improved muted-note label contrast. Updated the project and workspace documentation.

No pitch detector, model, API, cloud backend, instrument routing or dependency was added. The existing native Mac microphone changes were preserved. No microphone capture was started during this continuation.

## Commands actually executed

All paths are relative to this source folder. Commands below completed successfully unless a baseline failure is explicitly stated.

| Command | Result |
| --- | --- |
| `git status --short` | Folder has no Git repository metadata; no Git diff or commit claimed. |
| `build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2` before edits | Baseline build passed. |
| `build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure` before edits | Baseline 12/14 suites passed. Arrangement test aborted while searching for a schema-2 marker in schema-3 output; piano-roll assertions expected the single-note undo label. |
| `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2` | Updated Release desktop build passed. |
| `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure` | Updated desktop suites passed, 14/14. |
| `build/macos-tools/venv/bin/cmake --build --preset core` | Qt-free Debug core build passed. |
| `build/macos-tools/venv/bin/ctest --preset core` | Core suites passed, 10/10; these overlap with the desktop tests. |
| `python3 build/continuation-checks/run_workspace_check.py` | Local diagnostic uses real Open/Save controls: 960 PPQ, three lanes/regions, muted notes, metronome and grid survive save. Tempo changes update subsequent snap intervals without retiming notes. Also checks finite/looping PCM reads and seeks without audio hardware. |
| `PIP_NO_INDEX=1 LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh` | Builds and tests, deploys Qt, verifies Intel slices and bundle dependencies, locally signs/verifies the app, runs native Cocoa smoke checks, and refreshes the app/ZIP/checksum. Log: `build/macos-intel/continuation-build.log`. Uses the already-installed pinned dependencies with pip network access disabled. |

The repeated UI diagnostic initially timed out because selecting a file programmatically in the visible picker left the filename empty. The helper now fills the filename field as a user would; the final run passed. This required no application change.

The generated ZIP also passed `shasum -a 256 -c build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip.sha256`.

Visual review used generated offscreen screenshots in `build/continuation-checks/playlist.png` and `build/continuation-checks/piano-roll.png`. These are authored MIDI fixtures, not recordings. The diagnostic source and runner are retained in the same build directory.

## Local application

Open `build/macos-intel/packages/HumToSing.app`. The corresponding archive is `build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip`, with a SHA-256 sidecar. The app is locally ad-hoc signed, not Apple-notarized.

## Not verified by these checks

Real microphone permission/capture, sung-voice transcription accuracy, audible output, pause/resume/seek behavior on a physical audio device, native floating-window placement, and DAW import still require a manual trial. No claim of hardware or songwriter-beta acceptance is made from automated and offscreen checks.
