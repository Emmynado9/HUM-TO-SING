# Pitch Lab automated fixture results

Measured on 2026-10-10 in the Debian 13 x86_64 cloud environment with GCC 14.2.0 and a CMake Debug build. All audio in this report is deterministically generated 48 kHz mono; no microphone or human voice was available.

The test configuration matches the UI defaults: 4,096-frame window, 512-frame hop, 0.010 RMS gate, 80–1,000 Hz range, five-frame display median, and A4 = 440 Hz. Accuracy statistics exclude frame centers before the documented 150 ms warm-up.

| Fixture | Measured result |
|---|---:|
| 82.4069 Hz | 0.023 cents median absolute error |
| 110 Hz | 0.072 cents |
| 220 Hz | 0.105 cents |
| 261.6256 Hz | 0.377 cents |
| 440 Hz | 0.981 cents |
| 659.2551 Hz | 2.494 cents |
| 880 Hz | 2.048 cents |
| 987.7666 Hz | 5.864 cents |
| Silence | 0 reliable frames; every frame classified below gate |
| Fixed-seed uniform noise, amplitude ±0.20 | 0/86 reliable frames, 0% measured false-detection rate |
| 220 Hz with 8× amplitude change | median absolute error below 20 cents |
| 220 Hz, 5 Hz vibrato, ±30 cents | 0.247-cent median center bias; 100% reliable frames |
| 440 Hz at amplitude 0.002, gate 0.010 | no reliable frames |
| Same low-level tone, gate 0.001 | median absolute error below 20 cents |

The maximum steady-tone median error was 5.864 cents, within the milestone target of less than 20 cents. The fixed-seed result describes only this generated noise distribution; it is not a claim of general noise rejection.

Processing two seconds of 48 kHz mono took 0.414 seconds for 180 frames in the Debug build, a 0.207 realtime factor. A separately generated PCM16 A4 WAV replay produced 39/39 reliable frames and approximately +0.98 cents through `HumPitchFixture`.

At this configuration the detector needs an 85.3 ms audio window and advances by 10.7 ms. The UI refresh interval is up to 33 ms, and worker execution is measured separately on screen. Microphone-driver latency, human-voice accuracy, breathiness response, and octave-error rate remain unmeasured; use `pitch-lab-manual-checklist.md` on physical hardware.
