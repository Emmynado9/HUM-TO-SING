#!/bin/bash
# Local development build. No credentials, paid APIs, audio uploads or cloud jobs.
set -euo pipefail

usage() {
    cat <<'HELP'
Build HumToSing for an Intel Mac running macOS 13 Ventura or newer.

Usage: ./scripts/build-intel-mac.sh [--check]
  --check  Verify local prerequisites only; do not install or build anything.

Requires Apple's Command Line Tools and Python 3.9+.
The normal build downloads pinned CMake/aqt tools and official Qt 6.8.2 packages
into build/macos-tools, then builds, tests, deploys and locally signs the app.
Optional HUM_QT_PREFIX uses an existing Qt 6.8.2 macOS installation instead.
The resulting .app and ZIP are placed in build/macos-intel/packages.
HELP
}
fail() { printf '\nBuild stopped: %s\n' "$*" >&2; exit 1; }
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then usage; exit 0; fi
if [[ $# -gt 1 || ( $# -eq 1 && "$1" != "--check" ) ]]; then usage >&2; exit 2; fi

[[ "$(uname -s)" == "Darwin" ]] || fail "This script must run on your Mac. Linux cannot build this macOS app without Apple's SDK and macOS Qt libraries."
[[ "$(uname -m)" == "x86_64" ]] || fail "This build kit targets an Intel Mac (x86_64)."
mac_version="$(/usr/bin/sw_vers -productVersion)"
mac_major="${mac_version%%.*}"
[[ "$mac_major" =~ ^[0-9]+$ && "$mac_major" -ge 13 ]] || fail "macOS 13 Ventura or newer is required. Found $mac_version."
/usr/bin/xcode-select -p >/dev/null 2>&1 || fail "Install Apple's free Command Line Tools with: xcode-select --install. Finish that installation, then run this script again."
/usr/bin/xcrun --sdk macosx --find clang++ >/dev/null 2>&1 || fail "The selected developer tools have no usable macOS SDK/clang++. Select your installed Xcode or Command Line Tools and retry."
command -v python3 >/dev/null || fail "Install Python 3.9 or newer, then retry."
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,9) else 1)' || fail "Python 3.9 or newer is required."
for tool in /usr/bin/ditto /usr/bin/lipo /usr/bin/codesign /usr/bin/shasum; do
    [[ -x "$tool" ]] || fail "Required macOS tool is missing: $tool"
done
printf 'Local prerequisites found: Intel macOS %s, Apple SDK and Python 3.9+.\n' "$mac_version"
if [[ "${1:-}" == "--check" ]]; then exit 0; fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tools_dir="$repo_root/build/macos-tools"
build_dir="$repo_root/build/macos-intel"
qt_version="6.8.2"
mkdir -p "$tools_dir" "$build_dir/packages"
if [[ ! -x "$tools_dir/venv/bin/python" ]]; then python3 -m venv "$tools_dir/venv"; fi
python="$tools_dir/venv/bin/python"
"$python" -m pip install --disable-pip-version-check 'cmake==3.31.10' 'aqtinstall==3.3.0'
cmake="$tools_dir/venv/bin/cmake"
ctest="$tools_dir/venv/bin/ctest"

if [[ -n "${HUM_QT_PREFIX:-}" ]]; then
    qt_prefix="$HUM_QT_PREFIX"
else
    qt_prefix="$tools_dir/qt/$qt_version/macos"
    if [[ ! -x "$qt_prefix/bin/macdeployqt" || ! -d "$qt_prefix/lib/cmake/Qt6Multimedia" ]]; then
        "$python" -m aqt install-qt mac desktop "$qt_version" clang_64 \
            --outputdir "$tools_dir/qt" --modules qtmultimedia \
            --archives qtbase qtsvg qttools qttranslations
    fi
fi
[[ -x "$qt_prefix/bin/macdeployqt" ]] || fail "macdeployqt is missing at $qt_prefix/bin. Set HUM_QT_PREFIX to the root of a complete Qt 6.8.2 macOS installation."
[[ -d "$qt_prefix/lib/cmake/Qt6Multimedia" ]] || fail "The Qt Multimedia module is missing. Install it before retrying."
[[ "$("$qt_prefix/bin/qmake" -query QT_VERSION)" == "$qt_version" ]] || fail "Use Qt $qt_version for this build kit."

# Only build-time dependency preparation uses the network. The app has no
# download path; this separate engine loads its bundled CPU runtime on demand.
"$python" "$repo_root/scripts/prepare-neural-vocal.py"

"$cmake" -S "$repo_root" -B "$build_dir" -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DHUM_BUILD_GUI=ON \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_PREFIX_PATH="$qt_prefix"
"$cmake" --build "$build_dir" --parallel 2
"$ctest" --test-dir "$build_dir" --output-on-failure

source_app="$build_dir/bin/HumToSing.app"
[[ -x "$source_app/Contents/MacOS/HumToSing" ]] || fail "The build did not produce HumToSing.app."
/usr/bin/lipo "$source_app/Contents/MacOS/HumToSing" -verify_arch x86_64
"$python" - "$source_app/Contents/Info.plist" <<'PY'
import plistlib,sys
with open(sys.argv[1],'rb') as handle: info=plistlib.load(handle)
required={'CFBundleIdentifier':'com.hum-to-sing.sketchpad',
          'CFBundleShortVersionString':'0.1.0', 'CFBundleVersion':'0.1.0',
          'LSMinimumSystemVersion':'13.0'}
for key,value in required.items():
    if info.get(key)!=value: raise SystemExit('Invalid bundle metadata: '+key)
if not info.get('NSMicrophoneUsageDescription'):
    raise SystemExit('Missing microphone permission explanation')
PY

# Stage independently: failed deployment leaves a prior successful ZIP intact.
stage_dir="$(mktemp -d "$build_dir/package-stage.XXXXXX")"
staged_app="$stage_dir/HumToSing.app"
/usr/bin/ditto "$source_app" "$staged_app"
"$qt_prefix/bin/macdeployqt" "$staged_app" -always-overwrite -verbose=1
"$python" - "$staged_app" "$qt_prefix" "$build_dir" <<'PY'
import os,subprocess,sys
bundle,qt_prefix,build_dir=sys.argv[1:]
for directory,_,files in os.walk(bundle):
    for name in files:
        path=os.path.join(directory,name)
        kind=subprocess.check_output(['/usr/bin/file','-b',path],text=True)
        if 'Mach-O' not in kind: continue
        subprocess.run(['/usr/bin/lipo',path,'-verify_arch','x86_64'],check=True)
        linked=subprocess.check_output(['/usr/bin/otool','-L',path],text=True)
        rpaths='\n'.join(subprocess.check_output(['/usr/bin/otool','-l',path],text=True).splitlines()[1:])
        # The first otool -L line names this binary itself, not a dependency.
        if any(root in '\n'.join(linked.splitlines()[1:]) or root in rpaths
               for root in (qt_prefix,build_dir)):
            raise SystemExit('Deployed binary still references a development path: '+path)
print('Deployed Mach-O files contain Intel slices and no Qt/build path references.')
PY
mkdir -p "$staged_app/Contents/Resources/Documentation"
/usr/bin/ditto "$repo_root/docs/mac-intel-build.md" "$staged_app/Contents/Resources/Documentation/mac-intel-build.md"
/usr/bin/ditto "$repo_root/docs/dependencies-and-licenses.md" "$staged_app/Contents/Resources/Documentation/dependencies-and-licenses.md"
mkdir -p "$staged_app/Contents/Resources/Documentation/Vocal"
for notice in "$repo_root"/resources/vocal/*.txt; do
    /usr/bin/ditto "$notice" "$staged_app/Contents/Resources/Documentation/Vocal/$(basename "$notice")"
done
/usr/bin/ditto "$repo_root/docs/sing-to-midi.md" "$staged_app/Contents/Resources/Documentation/sing-to-midi.md"
/usr/bin/ditto "$repo_root/docs/voice-and-lyrics.md" "$staged_app/Contents/Resources/Documentation/voice-and-lyrics.md"
/usr/bin/ditto "$repo_root/docs/project-files.md" "$staged_app/Contents/Resources/Documentation/project-files.md"
/usr/bin/ditto "$repo_root/docs/overdub-recording.md" "$staged_app/Contents/Resources/Documentation/overdub-recording.md"
# Ad-hoc signing is free, local, and is not Apple Developer ID notarization.
/usr/bin/codesign --force --deep --sign - "$staged_app"
/usr/bin/codesign --verify --deep --strict "$staged_app"
/usr/bin/lipo "$staged_app/Contents/MacOS/HumToSing" -verify_arch x86_64
# Exercise the deployed app without developer Qt environment variables.
(
    unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH
    QT_QPA_PLATFORM=cocoa "$staged_app/Contents/MacOS/HumToSing" --smoke-test
    QT_QPA_PLATFORM=cocoa "$staged_app/Contents/MacOS/HumToSing" --vocal-backend-check
)

archive="$build_dir/packages/HumToSing-0.1.0-macOS13-Intel.zip"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$staged_app" "$stage_dir/package.zip"
# Publish local output only after compilation, all tests, deployment and signing succeed.
/usr/bin/ditto "$staged_app" "$build_dir/packages/HumToSing.app"
mv -f "$stage_dir/package.zip" "$archive"
/usr/bin/shasum -a 256 "$archive" > "$archive.sha256"
printf '\nBuild and tests completed on this Mac.\nApp: %s\nZIP: %s\n' "$build_dir/packages/HumToSing.app" "$archive"
printf 'Open HumToSing.app and allow Microphone access when recording.\n'
printf 'This evaluation build is locally signed; it is not notarized by Apple.\n'
