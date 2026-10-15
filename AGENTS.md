# Project guardrails

- Inspect the repository, existing instructions, dependencies, and build configuration before editing.
- Run the available baseline tests and build before changing code. If no baseline exists, record that explicitly.
- Name the files and components planned for change before editing.
- Keep changes scoped to the requested work and avoid unrelated modifications.
- Run the relevant tests and build after editing.
- Report commands that were actually executed separately from claims that remain unverified.
- Keep all audio and music processing local. Require no paid API or cloud backend, and never upload audio.
- Defer chords, multiple tracks, beatboxing, generative accompaniment, and VST3 until a later milestone.
- Keep the core independent of UI widgets and desktop-framework state so it can be tested alone and reused by future hosts.
- Use the existing checkout; do not create a Git worktree unless the user explicitly requests one.

## User-authorized scope update — 2026-10-14

- The user explicitly requested multiple Playlist track lanes and floating Hum-to-MIDI, piano-roll and Pitch Lab screens. The earlier multiple-track deferral does not block this scoped arrangement workspace.
- Preserve the canonical MIDI note model, reversible Magnet behavior, shared playback/export data and existing uncommitted work. Playlist regions reference canonical notes; do not create a second note model in a view.
- All lanes currently use the existing built-in instrument and merged format-0 MIDI export. Separate instrument routing/mixing, chord recognition, beatboxing, accompaniment and VST3 remain deferred.
- Spotify Basic Pitch evaluation/install was explicitly deferred by the user while the Playlist and floating screens are completed. Keep the existing local YIN detector for this scope.
