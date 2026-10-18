# Sampled instruments and chord progression editing

Mac playback now renders the system DLS sampled instrument bank through Core Audio, replacing the former harmonic approximations for piano and acoustic guitar. The existing electric piano, electric guitar and strings program selections also use that bank on Mac. The OS provides the bank; it is not downloaded or copied into the app. No network, paid service, microphone upload or external plugin is required. The existing portable synthesizer remains the non-Mac fallback.

`src/audio/MacInstrumentRenderer` is an offline platform adapter accepting core notes and instrument assignments. It has no Qt widgets or window state. Audio renders before playback; all voices of a chord mix into one continuous PCM buffer. Scheduled note-offs retain a 1.2-second tail, the complete mix receives constant peak reduction instead of hard clipping, and the buffer ends with a short fade. The sink has a 100-ms buffer and only treats Idle as completion when its source has ended. Main arrangement playback, metronome and pause/resume continue using the shared transport.

The piano roll's **Track instrument** selector changes the selected Playlist track, supports Undo and persists through project saving. Clicking a white or black key on the left auditions that exact pitch with the selected track's instrument, independently of arrangement playback, without editing notes. Each click sounds for 0.8 seconds plus its natural release.

In **Hum → MIDI → Chord progression**:

- Hummed roots snap to the nearest selected-scale note above or below. Equal-distance ties choose the lower pitch.
- **Smooth chords** is on by default. New conversions hold a root until the following root, and merge repeated roots, preventing short detection gaps from repeatedly cutting/restarting the chord. Disable it to preserve hummed rests and repeated attacks. Previously converted regions are not rewritten automatically; reconvert a saved take or record again to apply this timing option.
- Click degree buttons or type a progression such as `I IV V I`, `i-iv-v-i`, or `1 4 5 1`. Roman case is accepted flexibly; chord quality comes from the selected major/natural-minor scale. Labels show the actual chord names and conventional numeral quality. In C major: I=C major, ii=D minor, iii=E minor, IV=F major, V=G major, vi=A minor, vii°=B diminished.
- Choose beats per chord and click **Add progression to Playlist**. Notes append to a Chords track as a normal undoable canonical region. Their timing, playback, editing, MIDI export and project saving use the existing note model.

New projects enable nearest-key melody snapping through reversible Magnet. The Melody tab now exposes that setting directly. Existing project files retain their saved Magnet setting. The shared song key and scale are still editable in the chord tab or piano roll. Snapping preserves canonical melody pitches underneath the projected view.

Reference documentation: [Apple sampled instrument loading](https://developer.apple.com/library/archive/technotes/tn2283/_index.html), [Apple DLS synth](https://developer.apple.com/documentation/audiotoolbox/kaudiounitsubtype_dlssynth), and [diatonic Roman numeral analysis](https://www.musictheory.net/lessons/44).

## Verification

Baseline desktop build and 16/16 tests passed before edits. After edits, a clean desktop rebuild and all 17 tests passed. The new Mac audio suite renders both actual instrument programs and checks distinct PCM, audible attacks, sustained chord continuity, release tails, bounded samples, silence for muted notes and invalid routing. Core/UI regressions cover root-gap joining, preserved rests, nearest-scale snapping, Roman input validation, chord timing and quality, undoable instrument changes, piano-key pitch mapping through zoom/scroll, and key clicks not editing notes. The app smoke test operates the actual progression and instrument controls.

Commands used (build/test commands with `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8`):

```sh
build/macos-tools/venv/bin/cmake --build build/macos-intel --parallel 2
build/macos-tools/venv/bin/ctest --test-dir build/macos-intel --output-on-failure
build/macos-tools/venv/bin/cmake --build build/macos-intel --clean-first --parallel 2
```

Logs: `build/instruments-clean-build.log`, `build/instruments-tests.log`. During development an incorrect upward-snap test expectation was corrected: C in D major is equally distant from B and C#, so the specified lower tie-break correctly selects B. The first fixed-time hardware diagnostic failed its early clock assertion during instrument startup; the subsequent run passed. These intermediate failures are not evidence of live microphone performance.

`python3 build/chord-playback-checks/run.py` passed against the clean build. It loaded a chord project through the real Open dialog, entered I–IV–V–I, selected acoustic guitar, saved, verified the exact appended pitches/times and stored instrument, and exercised Play/Pause/Space-resume/Stop using the actual output device. Log: `build/chord-playback-checks/result.log`. That workflow also exposed and led to a fix for default Magnet initialization incorrectly marking a fresh project as modified.

Live human humming and subjective instrument realism require listening/manual validation. Synthetic and rendered-audio checks establish pipeline and buffer behavior, not accuracy for every voice or acoustic environment.

## Packaged result

`PIP_NO_INDEX=1 LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh` completed with 17/17 passing tests, Intel/dependency checks, local signing and the packaged native-window smoke test. The final UI pass keeps Undo/Redo labels short and places the action description in their tooltips. Log: `build/instruments-package.log`.

`LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 shasum -a 256 -c build/macos-intel/packages/HumToSing-0.1.0-macOS13-Intel.zip.sha256` reported OK. Reopen `build/macos-intel/packages/HumToSing.app` to use the updated build. The matching ZIP is in the same directory.
