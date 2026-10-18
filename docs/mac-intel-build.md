# Try HumToSing on an Intel Mac running Ventura

This kit contains the project source, including the Playlist and floating screens, and a local Mac builder. The repaired builder has now compiled, tested, packaged and launched the application on an Intel Mac running macOS 13.7.8. The generated application is `build/macos-intel/packages/HumToSing.app`; copy it to Applications to install it. The user has confirmed microphone recording works. A later repair negotiates a supported speaker format for MIDI and original-audio playback; see [audio and toolbar results](audio-toolbar-fix-2026-10-17.md).

## Build on your Mac

1. Extract `HumToSing-Intel-Mac-Ventura-build-kit.zip` to a normal writable folder, such as Downloads. Keep the folder's files together.
2. Open Terminal and run `xcode-select --install` if Apple's free Command Line Tools are not installed. Finish that installer before continuing. You also need Python 3.9 or newer (`python3 --version`). Python from a compatible Command Line Tools installation or python.org is sufficient; Homebrew is not required.
3. Double-click **Build-Intel-Mac.command**. If Finder does not allow the script to open, use Terminal from the extracted folder:

   ```sh
   bash scripts/build-intel-mac.sh
   ```

4. The script downloads pinned build tools and official Qt 6.8.2 macOS libraries into `build/macos-tools`. Development setup needs internet access and space for the Qt SDK/build. It uses CMake 3.31.10, aqtinstall 3.3.0 and Qt Multimedia, with the Base, SVG, Tools and Translations archives. The unused Qt Declarative archive is omitted. It does not install a cloud backend, upload audio, request credentials or use a paid service. Existing system Qt/compilers are not overwritten.
5. After compilation and all automated tests pass, the script deploys Qt into **build/macos-intel/packages/HumToSing.app**, performs local ad-hoc code signing and a deployed-app smoke test, and creates **HumToSing-0.1.0-macOS13-Intel.zip** plus a checksum. A failed build does not claim success or replace a previous successful ZIP.
6. Double-click the generated `.app`. If macOS blocks this unnotarized evaluation app, use Finder's Open command or Privacy & Security's Open Anyway for this app. Do not disable Gatekeeper globally. Copy the `.app` to Applications if you want to keep it there.

Prerequisite check without downloads or compilation:

```sh
bash scripts/build-intel-mac.sh --check
```

For an already-installed **Qt 6.8.2 macOS SDK**, skip Qt downloading:

```sh
HUM_QT_PREFIX="/path/to/Qt/6.8.2/macos" bash scripts/build-intel-mac.sh
```

The target is `x86_64`, with a minimum macOS version of **13.0**. This build kit targets Intel Macs; Apple Silicon and older macOS versions are not verified. The `.app` includes its Qt runtime through `macdeployqt`, so normal use should not require the development SDK or internet. The script tests the deployed app with the native Cocoa platform plugin and development Qt/DYLD environment overrides removed. Run it from a logged-in Mac desktop session; the launch test briefly opens the app's windows. The development test suite still uses the SDK's offscreen plugin. The Mac startup test allows 30 seconds for first-launch initialization.

## First trial

The Playlist shows the selected system output and a **Test speakers** button. If needed, choose the desired output in macOS Sound settings. In **Hum → MIDI**, enable **Metronome** and choose BPM to hear a click while recording; headphones keep it out of the microphone.


Open **Hum → MIDI**, choose **Enable microphone** and allow macOS microphone access. Select the microphone, Record, hum a short single-note melody and Stop. Play original to hear your voice; close that window and inspect the Playlist region. Open Piano roll, correct a note, Undo/Redo, toggle Magnet, and Play melody. Save/reopen the project and export MIDI. The app must appear under System Settings → Privacy & Security → Microphone after permission is requested. Original audio remains in memory for the latest take and is not embedded in project files.

If anything fails, copy the Terminal error text or the app's displayed error. The existing synthetic/fake-device tests do not establish real Mac microphone accuracy or audible playback.

## Mac verification — 2026-10-17

Environment: Intel macOS 13.7.8, Apple Clang 14.0.3, macOS 13.3 SDK, Python 3.9.6, CMake 3.31.10 and Qt 6.8.2. This is a personal evaluation build with local ad-hoc signing, not Apple notarization.

### Commands executed before repairs

- `cmake --preset core` and `ctest --preset core` initially failed because the tools were not on PATH. The builder installed its pinned tools into the project-local virtual environment.
- `build/macos-tools/venv/bin/cmake --preset core`, `build/macos-tools/venv/bin/cmake --build --preset core` and `build/macos-tools/venv/bin/ctest --preset core`: build succeeded, 10/10 tests passed.
- `bash scripts/build-intel-mac.sh`: the desktop build succeeded. The first run passed 13/14 tests and timed out during first app startup; a focused retry passed. The full retry passed 14/14 tests, then failed the deployment-path check. Logs: `build/macos-intel/baseline-build.log` and `build/macos-intel/baseline-retry.log`.

### Repairs and completed checks

- The dependency check now ignores the `otool -l` filename header. A Python regression check against a real Mach-O fixture accepted its bundle filename and rejected an actual development `LC_RPATH`.
- The packaged launch check uses Cocoa. `macdeployqt` does not include the SDK's offscreen plugin in this app.
- `qt_add_executable` enables Qt's permission finalizer to link the microphone backend. The plist uses CMake's `${MACOSX_BUNDLE_...}` placeholders so Qt's preliminary template pass preserves the bundle metadata. The resulting native launch no longer reports a missing `QMicrophonePermission` plugin.
- The Mac startup test allows 30 seconds; other platforms retain the existing timeout. The downloader omits unused Qt Declarative libraries.
- `bash -n scripts/build-intel-mac.sh`: passed.
- `LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 bash scripts/build-intel-mac.sh`: completed successfully, including the Release build, 14/14 CTest tests, Intel architecture checks, bundle metadata and dependency checks, `codesign --verify --deep --strict`, native packaged-app smoke test, ZIP creation and SHA-256 checksum. Log: `build/macos-intel/repaired-build.log`.

Original versions of the changed files are retained in `build/build-repair-backup`. `SOURCE-MANIFEST.sha256` remains the original source-kit manifest; it does not describe the repaired files.

### Not yet verified

Real microphone permission approval, recording quality, audible original/melody playback, and a manual save/export/DAW round trip require the first trial above. No recording was made or uploaded during the build. Apple Silicon, other macOS versions, and notarized distribution were not tested. The earlier Linux results remain historical evidence, not a Linux rerun of these repairs.

Reference sources: Qt [supported platforms](https://doc.qt.io/qt-6.8/supported-platforms.html), [macOS deployment](https://doc.qt.io/qt-6.8/macos-deployment.html), [aqtinstall 3.3.0](https://pypi.org/project/aqtinstall/3.3.0/), [CMake 3.31.10 Python package](https://pypi.org/project/cmake/3.31.10/) and the existing [dependency review](dependencies-and-licenses.md).
