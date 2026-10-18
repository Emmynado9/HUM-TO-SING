# Neural vocal dependency provenance

- SwiftF0 upstream: https://github.com/lars76/swift-f0
- Pinned revision: `d1ba77fe5331310b778ebe126b6cb3656d41d24f`.
- Model: `swift_f0/model.onnx`, 135,090 bytes.
- Model SHA-256: `6385e8c2ebc3872e82c9ff5946228de44cd3be77a750ea53698b7dbfe94b0a22`.
- License: MIT, reproduced in `SwiftF0-LICENSE.txt`.
- Runtime: official `onnxruntime-osx-x86_64-1.20.1.tgz` from https://github.com/microsoft/onnxruntime/releases/tag/v1.20.1.
- Archive SHA-256: `0f73006813af2a1a5d1723ed7dfb694fc629d15037124081bb61b7bf7d99fc78`.
- Unmodified runtime library SHA-256: `2d2306de2ea691044b0afa552d1b8ffb0e51baee3172d0988be587f0b68f27a0`.
- Runtime license: MIT plus bundled third-party notices, reproduced in the adjacent files. Local ad-hoc signing changes the packaged library's signature/hash.

The model is embedded in the app. The native runtime is kept under ignored `build/neural-vocal` for development and copied into the app's Frameworks directory. This model is not trained or modified by the application. Runtime loading is deferred until conversion so macOS 13.3's runtime requirement does not change startup behavior of the existing engine on macOS 13.0. No model or audio download happens in the app.
