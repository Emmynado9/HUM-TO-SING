# Dependency and license review

Checked against upstream project sources on 2026-10-10. This is an engineering record, not legal advice.

## Current dependencies

| Dependency | Version used here | Purpose | Upstream license evidence | Distribution note |
|---|---:|---|---|---|
| Qt Base (Core, Gui, Widgets, Concurrent) | 6.8.2 | Desktop shell and playback conversion worker | Qt's `QWidget` source declares `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`; the corresponding LGPL text is in the official source tree. | This foundation assumes dynamic linking under LGPLv3. A shipped build needs the required notices/license texts and must preserve user relinking and reverse-engineering rights. A commercial Qt license is another option. |
| Qt Multimedia | 6.8.2 | Microphone enumeration/capture and audio verification playback | `QAudioSource` source declares `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. | The same dynamic-linking and notice obligations apply. Platform multimedia backends and their redistributed codecs/plugins must be audited during packaging. This milestone processes uncompressed PCM and adds no codec dependency. |
| FFmpeg system runtime selected by Qt | 7.1.5-0+deb13u1 in this environment | Qt's Debian multimedia plugin initializes this backend even for the launch smoke test | `ffmpeg -L` reports that this Debian build is GPL version 2 or later because it was configured with GPL components. Upstream explains that FFmpeg is normally LGPL but becomes GPL when GPL parts are enabled. | It is supplied by the development OS and is not bundled by this repository. A distribution must either comply with the exact FFmpeg build's license or select/package a compatible Qt backend; do not copy this Debian runtime into a closed-source bundle without a specific license review. |
| CMake | 3.31.10 in this environment; minimum 3.25 | Build/test/package tooling | Official `Copyright.txt` grants BSD-style source and binary redistribution terms. | Build tool only; not linked into the application. |
| C++ standard library | Compiler/platform supplied | Core runtime | Toolchain-specific | Confirm the chosen platform toolchain and runtime redistribution terms during packaging. |

Official evidence:

- Qt widget SPDX declaration: https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/kernel/qwidget.h
- Qt microphone input documentation: https://doc.qt.io/qt-6.8/qaudiosource.html
- Qt permission documentation: https://doc.qt.io/qt-6.8/permissions.html
- Qt Multimedia SPDX declaration: https://github.com/qt/qtmultimedia/blob/v6.8.2/src/multimedia/audio/qaudiosource.cpp
- Qt LGPLv3 text: https://github.com/qt/qtbase/blob/v6.8.2/LICENSES/LGPL-3.0-only.txt
- FFmpeg license and compliance information: https://ffmpeg.org/legal.html
- CMake terms: https://github.com/Kitware/CMake/blob/v3.31.10/Copyright.txt

The repository itself has no owner-selected license yet. No dependency license grants permission to redistribute this project's source.

The note-extraction and Standard MIDI File milestone adds only repository-owned C++ code and standard-library use. It reuses the selected local YIN detector and Qt Concurrent worker support, so there is no new package, model, transitive runtime, or license obligation.

The piano-roll milestone also adds only repository-owned C++ and Qt Widgets code. Its offscreen interaction test uses Qt Test from the existing Qt Base distribution under the same Qt licensing choices recorded above; it adds no production runtime dependency, model, service, or network access.

The scale-guidance and reversible Magnet milestone adds only repository-owned standard C++ and Qt Widgets code. It uses the existing interval model and introduces no library, model, API, asset, or additional license obligation.

The single-track MVP adds only repository-owned C++/Qt code. The built-in soft-tone instrument generates sine waves mathematically at runtime, so there is no soundfont, sample library, model, preset, or third-party audio asset to license or download. Project persistence uses Qt Core's `QSaveFile`, already covered by the Qt Base licensing choice above. The Debian package dynamically links system Qt and declares Debian runtime dependencies; it does not copy Qt or FFmpeg libraries into the package.

The repository still has no owner-selected license. The generated package is suitable for controlled evaluation by the copyright holder, but a public source or binary release needs an explicit project license and a final notice/compliance review. The Debian package is unsigned. These are release decisions, not runtime cloud dependencies.

## Reviewed future dependencies and alternatives

The separate Piano Roll Sing to MIDI feature now embeds the SwiftF0 model from revision `d1ba77fe5331310b778ebe126b6cb3656d41d24f` and loads the official ONNX Runtime 1.20.1 Intel Mac library on demand. Both declare MIT licensing. Exact provenance/hashes and copied licenses, including ONNX Runtime's ThirdPartyNotices, are in `resources/vocal/`; the Mac package includes the notices. The model and runtime run locally, with no Python dependency or network access in the app. Native library inspection found a macOS 13.3 minimum; the original engine remains independent of it. Sources: https://github.com/lars76/swift-f0/blob/d1ba77fe5331310b778ebe126b6cb3656d41d24f/LICENSE and https://github.com/microsoft/onnxruntime/tree/v1.20.1.

Quality evaluation used three locally downloaded Vocadito recordings, CC BY 4.0, from Rachel Bittner, Katherine Pasalo, Juan José Bosch, Gabriel Meseguer Brocal and David Rubinstein (2021), https://doi.org/10.5281/zenodo.5578807. Audio and source annotations remain under ignored build storage and are not distributed with the application. Results and attribution are recorded in `docs/sing-to-midi-results.md`.

- Spotify Basic Pitch's current repository declares Apache-2.0 and its `NOTICE` lists the separate licenses of its Python/runtime dependencies. Its own README describes a lightweight neural network for file-based, polyphonic automatic music transcription and ships TensorFlow, CoreML, TensorFlow Lite, and ONNX model serializations. It was not adopted for the single-note live diagnostic because the model/runtime and transcription workflow add avoidable packaging and latency complexity. If used for later offline transcription, pin an exact release and re-audit the selected model artifact plus all transitive runtime licenses. Sources checked: https://github.com/spotify/basic-pitch/blob/main/LICENSE, https://github.com/spotify/basic-pitch/blob/main/NOTICE, and https://github.com/spotify/basic-pitch/blob/main/README.md.
- aubio directly supports live pitch methods, configurable buffer/hop sizes, silence thresholds, and a detector confidence value. Its repository, README, `COPYING`, and pitch header declare GPL-3.0-or-later. It was not linked because the project has no chosen GPL distribution model. Sources checked: https://github.com/aubio/aubio/blob/master/COPYING, https://github.com/aubio/aubio/blob/master/README.md, and https://github.com/aubio/aubio/blob/master/src/pitch/pitch.h.
- The selected Pitch Lab detector is an original C++ implementation of the published YIN difference-function method, with a local box-average decimation stage. It introduces no software library or model dependency. Algorithm reference: Alain de Cheveigné and Hideki Kawahara, “YIN, a fundamental frequency estimator for speech and music,” JASA 111(4), 2002, https://doi.org/10.1121/1.1458024. The paper is a technical reference and is not copied into the repository.
- JUCE 9.0.3's official `LICENSE.md` says its modules are dual-licensed under AGPLv3 and the commercial JUCE 9 license: https://github.com/juce-framework/JUCE/blob/9.0.3/LICENSE.md. Requests to the current JUCE pricing/EULA pages returned HTTP 403 from this environment, so pricing tiers and commercial eligibility remain unverified. No pricing claim from the blueprint should be relied upon until reviewed directly.
- SDL 3.4.18 uses the permissive zlib license, including commercial use: https://github.com/libsdl-org/SDL/blob/release-3.4.18/LICENSE.txt. It was considered but is not a dependency.
- The current official VST3 SDK `LICENSE.txt` on its default branch is MIT, copyright 2026 Steinberg: https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt. VST3 is explicitly deferred and the SDK is not a dependency. Recheck the exact pinned release and all bundled components before that milestone.

Upstream tagged releases observed during review included Qt 6.12.0, JUCE 9.0.3, and SDL 3.4.18. The application uses the Qt 6.8.2 packages available on the tested Debian platform; it does not claim validation against those newer releases.
