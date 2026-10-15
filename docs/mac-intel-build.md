# Try HumToSing on an Intel Mac running Ventura

This kit contains the latest project source, including the Playlist and floating screens. **It is not a precompiled Mac application.** The cloud workspace is Linux, with no Apple SDK or macOS Qt runtime. Access to the connected repository's GitHub API also returned Forbidden, so a remote macOS build could not be started. Compilation, packaging, native audio and launch on macOS remain unverified until this kit runs on a Mac.

## Build on your Mac

1. Extract `HumToSing-Intel-Mac-Ventura-build-kit.zip` to a normal writable folder, such as Downloads. Keep the folder's files together.
2. Open Terminal and run `xcode-select --install` if Apple's free Command Line Tools are not installed. Finish that installer before continuing. You also need Python 3.9 or newer (`python3 --version`). Python from a compatible Command Line Tools installation or python.org is sufficient; Homebrew is not required.
3. Double-click **Build-Intel-Mac.command**. If Finder does not allow the script to open, use Terminal from the extracted folder:

   ```sh
   bash scripts/build-intel-mac.sh
   ```

4. The script downloads pinned build tools and official Qt 6.8.2 macOS libraries into `build/macos-tools`. Development setup needs internet access and space for the Qt SDK/build. It uses CMake 3.31.10, aqtinstall 3.3.0 and Qt Multimedia. It does not install a cloud backend, upload audio, request credentials or use a paid service. Existing system Qt/compilers are not overwritten.
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

The target is `x86_64`, with a minimum macOS version of **13.0**. This build kit targets Intel Macs; Apple Silicon and older macOS versions are not verified. The `.app` includes its Qt runtime through `macdeployqt`, so normal use should not require the development SDK or internet. That standalone runtime claim must be checked on the actual Mac; the script runs an offscreen smoke test with development Qt/DYLD environment overrides removed.

## First trial

Open **Hum → MIDI**, select the MacBook microphone, Record, allow macOS microphone access, hum a short single-note melody and Stop. Play original to hear your voice; close that window and inspect the Playlist region. Open Piano roll, correct a note, Undo/Redo, toggle Magnet, and Play melody. Save/reopen the project and export MIDI. The app must appear under System Settings → Privacy & Security → Microphone after permission is requested. Original audio remains in memory for the latest take and is not embedded in project files.

If anything fails, copy the Terminal error text or the app's displayed error. The existing synthetic/fake-device tests do not establish real Mac microphone accuracy or audible playback.

## What is actually verified here

- Linux release baseline: 14/14 automated tests passed before changing packaging.
- Build-script shell syntax, Linux rejection before installation, plist template values and source-kit contents are checked separately. These are not macOS compilation results.
- Qt 6.8.2 is the existing project version. aqtinstall 3.3.0 is an MIT-licensed development tool; its PyPI metadata requires Python 3.9+. The CMake 3.31.10 PyPI release provides a macOS 10.10+ universal2 wheel and requires Python 3.7+. The repository's existing Qt licensing/distribution review remains applicable. Qt platform/deployment documentation could not be fetched from this workspace (proxy returned Forbidden); macOS packaging behavior must therefore be validated on the Mac.
- No Mac `.app`, installer, notarization or successful real-hardware recording was generated or claimed in the Linux workspace. This is a personal evaluation build workflow, not a public release license/compliance certification.

Reference sources: Qt [supported platforms](https://doc.qt.io/qt-6.8/supported-platforms.html), [macOS deployment](https://doc.qt.io/qt-6.8/macos-deployment.html), [aqtinstall 3.3.0](https://pypi.org/project/aqtinstall/3.3.0/), [CMake 3.31.10 Python package](https://pypi.org/project/cmake/3.31.10/) and the existing [dependency review](dependencies-and-licenses.md).
