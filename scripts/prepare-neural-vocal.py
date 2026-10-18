#!/usr/bin/env python3
"""Prepare pinned build dependencies. Never runs in the app or transfers audio."""
import hashlib
import pathlib
import platform
import shutil
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPS = ROOT / "build/neural-vocal"
MODEL = ROOT / "resources/vocal"
REVISION = "d1ba77fe5331310b778ebe126b6cb3656d41d24f"
ARCHIVE = "onnxruntime-osx-x86_64-1.20.1.tgz"


def fetch(path, url, checksum):
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == checksum:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    cached = ROOT / "build/sing-midi-research" / path.name
    if cached.exists() and hashlib.sha256(cached.read_bytes()).hexdigest() == checksum:
        data = cached.read_bytes()
    else:
        with urllib.request.urlopen(url, timeout=90) as response:
            data = response.read()
    if hashlib.sha256(data).hexdigest() != checksum:
        raise RuntimeError(f"Checksum mismatch: {path.name}")
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_bytes(data)
    temp.replace(path)


def main():
    if platform.system() != "Darwin" or platform.machine() != "x86_64":
        raise SystemExit("This dependency preparation targets Intel Mac. Supply HUM_ONNXRUNTIME_ROOT for another native build.")
    fetch(DEPS / ARCHIVE,
          f"https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/{ARCHIVE}",
          "0f73006813af2a1a5d1723ed7dfb694fc629d15037124081bb61b7bf7d99fc78")
    runtime = DEPS / "onnxruntime-osx-x86_64-1.20.1"
    library = runtime / "lib/libonnxruntime.1.20.1.dylib"
    if not library.exists() or hashlib.sha256(library.read_bytes()).hexdigest() != "2d2306de2ea691044b0afa552d1b8ffb0e51baee3172d0988be587f0b68f27a0":
        with tarfile.open(DEPS / ARCHIVE) as archive:
            for member in archive.getmembers():
                path = pathlib.PurePosixPath(member.name)
                link = pathlib.PurePosixPath(member.linkname)
                if path.is_absolute() or ".." in path.parts or link.is_absolute() or ".." in link.parts:
                    raise RuntimeError("Unsafe runtime archive entry")
            archive.extractall(DEPS)
    fetch(MODEL / "swift-f0.onnx",
          f"https://raw.githubusercontent.com/lars76/swift-f0/{REVISION}/swift_f0/model.onnx",
          "6385e8c2ebc3872e82c9ff5946228de44cd3be77a750ea53698b7dbfe94b0a22")
    fetch(MODEL / "SwiftF0-LICENSE.txt",
          f"https://raw.githubusercontent.com/lars76/swift-f0/{REVISION}/LICENSE",
          "5f4711c748cf616e894fbab28144ee74f1650dfe1bbd6801c44f11e054508c18")
    shutil.copyfile(runtime / "LICENSE", MODEL / "ONNXRuntime-LICENSE.txt")
    shutil.copyfile(runtime / "ThirdPartyNotices.txt", MODEL / "ONNXRuntime-ThirdPartyNotices.txt")
    print(f"Pinned vocal model and Intel Mac runtime ready: {runtime}")


if __name__ == "__main__":
    main()
