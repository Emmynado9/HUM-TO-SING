# Voice-to-instrument quality trial

This is a personal, noncommercial MusicGen Melody experiment, separate from the desktop app. It tests creative interpretations of humming on **piano, acoustic guitar and flute**, followed by local transcription of the generated melody into MIDI. It does not promise faithful conversion, isolated instruments, accurate generated MIDI, exact tempo or real-time results.

The app now includes an **AI Trial** screen for this workflow; the `.humproj` schema is unchanged. The local command-line tool never accesses the network. Only a manual upload in the interactive Colab notebook sends a selected excerpt to the cloud. No paid resources, accounts with billing, automatic model substitution or model training are required.

## Test from the desktop app

1. Use **Record voice** in the Playlist to capture a source phrase. Select the MIDI track where you want the result's editable notes.
2. Click **AI Trial** beside **Lyrics**. Select the recorded clip, instrument, optional style, excerpt start and length. The prompt is assembled automatically.
3. Click **Save trial files…** and choose a folder. Each export creates a fresh subfolder containing only `guide.wav` and `request.json`; it never overwrites a prior trial or uploads files. **Show files** opens that folder.
4. Click **Open free Colab trial**. In the browser, select a free GPU and run the notebook. Upload only the two files when asked. Download and unzip its result ZIP.
5. Back in AI Trial, click **Import generated WAV…** and select `generated.wav`. Local conversion runs in the background and can be cancelled. Compare **Play generated audio** with **Play MIDI preview**.
6. Set **Insert at**, then choose **Add MIDI to selected track** or **Add generated audio**. You can add both for comparison, but mute one during playback. Save the project to retain inserted results.

The insert position initially follows the selected source excerpt. MIDI preview uses the destination track's existing built-in instrument; generated audio preserves the actual requested AI sound, including flute. Adding MIDI does not change other notes or the track's instrument, and one Undo removes the inserted region. Generated audio uses the existing voice-clip storage and mute/delete controls. Duplicate clicks cannot insert the same preview twice. Closing cancels pending conversion; reopening starts a fresh trial, with exported/downloaded files left intact.

No terminal commands are needed for this app workflow. You still run generation manually in the Colab browser: there is no paid API, direct app upload or always-on backend. If there are no saved voice clips, the screen explains how to create one and still allows importing an existing generated WAV. Actual AI generation and human listening quality remain unverified; the UI tests use synthetic audio.

## 1. Build the local tool

From the checkout on this Mac:

```sh
build/macos-tools/venv/bin/cmake --build --preset core
build/core/bin/HumGenerationTrial --help
```

On another machine, configure with `cmake --preset core` first. The existing native neural dependency must be available for `transcribe`; `prepare` and `list` work without it. A missing runtime stops transcription, without falling back to YIN. This tool uses the existing checked-in SwiftF0 model and configured ONNX Runtime, not a new cloud transcription service.

## 2. Choose and prepare your own voice excerpt

Use **Record voice** in the app, then **Save project**. `Hum → MIDI` source recordings are session-only and are not saved voice clips. List available saved clips:

```sh
build/core/bin/HumGenerationTrial list --project /path/to/song.humproj
```

Choose a **one-based clip number** explicitly. Export to a fresh folder under ignored `trial-data/` or `build/`:

```sh
build/core/bin/HumGenerationTrial prepare \
  --project /path/to/song.humproj --clip 1 \
  --instrument piano --style gentle --seed 42 \
  --offset 0 --duration 8 \
  --output trial-data/sustain-piano
```

The directory contains only `guide.wav` and `request.json`. The WAV is the chosen mono PCM16 excerpt, even if its Playlist lane is muted. No project lyrics, other tracks, clip name or project path are included. The project remains unchanged.

- Instruments: `piano`, `acoustic-guitar`, `flute`.
- Styles: `neutral` (default), `gentle`, `bright`, `melancholic`, `energetic`.
- `offset` is seconds into this clip, not the Playlist position. It defaults to zero.
- Duration defaults to 10 seconds and is capped at the clip's remaining audio. Excerpts must contain at least 0.5 seconds; longer-than-10-second requests and effectively silent excerpts are rejected.
- Project tempo is included as descriptive guidance. It does not force generated timing.
- The default seed is 42. A seed does not guarantee identical output across hardware/library changes.
- Existing output directories are never overwritten. Use a new folder for every retry.

Prompts are deterministic templates. For example:

> Solo piano, expressive single-note melody, gentle phrasing, sparse unaccompanied performance, one melodic line, 120 BPM.

The recording is supplied separately as the melody guide; prose does not encode its exact notes. No language-model API is involved.

## 3. Run the free cloud trial interactively

Open [the trial in Colab](https://colab.research.google.com/github/Emmynado9/HUM-TO-SING/blob/main/experiments/audiocraft/voice_to_instrument_trial.ipynb), or upload [voice_to_instrument_trial.ipynb](voice_to_instrument_trial.ipynb) in Google Colab using **File → Upload notebook**, or open the notebook from GitHub. Choose a **free GPU runtime**, then run cells in order.

The notebook checks GPU availability before installation or upload. It creates an isolated Python 3.10 environment and installs AudioCraft at commit `896ec7c47f5e5d1e5aa1e4b260c4405328bf009d` with [compatibility constraints](constraints.txt). This avoids replacing Colab's notebook Python stack. The exact installed versions and resolved MusicGen model revision are recorded in each result.

Upload exactly `request.json` and `guide.wav` from **one** prepared folder when asked. Inspect the printed prompt, run one generation, listen, and download the result ZIP. The ZIP contains:

- `generated.wav`: PCM16, 32-kHz mono generated audio;
- `result.json`: prompt, controls, seed, model revision, guide hash, GPU, timings and output gain;
- `environment.json`: Python and installed package versions;
- `SUCCESS`: confirms generation completed, not that quality passed.

Free GPU availability, installation compatibility with the allocated runtime, memory usage and generation time must be measured there. Setup or generation errors stop without retries, CPU substitution, text-only model substitution or paid upgrades. No credits are purchased. The notebook is not an always-on app backend and exposes no HTTP service or tunnel.

Download results before disconnecting. Do not mount Drive or share a notebook containing recordings or output cells. Finish with **Runtime → Disconnect and delete runtime**. The checked-in notebook contains code only and embeds its runner so no separate helper upload is needed.

## 4. Transcribe the generated melody locally

Unzip the downloaded result into an ignored local folder. Use the tempo and instrument from its `result.json`:

```sh
build/core/bin/HumGenerationTrial transcribe \
  --audio trial-data/sustain-piano-result/generated.wav \
  --bpm 120 --instrument piano \
  --output trial-data/sustain-piano-midi
```

This invokes the existing neural engine with its natural preset. It does not quantize or force notes into a scale. Outputs:

- `generated.mid`: canonical MIDI serialization, with a GM program for the selected instrument;
- `notes.csv`: detected pitches, timing and velocity for inspection;
- `midi-reference.wav`: the detected notes rendered with a **generic built-in tone**, for timing/pitch comparison rather than timbre comparison;
- `transcription.json`: engine, preset, tempo, note count, processing time and `quality: unrated`.

No notes, a missing neural runtime or invalid audio causes a clear failure. The source generated audio is never replaced. Instrument transcription is experimental: chords, unwanted accompaniment or expressive pitch movement can defeat the monophonic engine. Use a MIDI editor to assess correction effort; this trial does not add app MIDI/audio import controls.

## 5. Evaluate all nine combinations

Prepare three excerpts you are willing to upload: `sustain`, `repeated-rests`, and `pitch-jumps`. Generate each on all three instruments. Use seed 42 and neutral style for the initial nine comparisons; optional-style experiments are separate retries, not replacements for poor initial scores.

Copy [evaluation-template.csv](evaluation-template.csv) into `trial-data/` and fill it in after listening:

- Instrument accuracy: 1–5; 4 means a clear requested solo instrument without substantial unwanted accompaniment.
- Melodic relevance: 1–5; 4 means your hummed idea remains recognizable despite creative changes.
- Musical usefulness: 1–5; 4 means the phrase is usable with musically sensible holds and pauses.
- MIDI corrections: `none`, `minor`, `major`, or `unusable`. Minor means occasional note/timing fixes; major means recurring errors or rewriting sections. Rate against the **generated audio**, not the original hum.
- Enter generation time from `result.json`, actual correction minutes (zero if none), and observations.

```sh
python3 experiments/audiocraft/evaluate.py trial-data/evaluation.csv
```

An incomplete sheet returns `not_evaluated`. All nine rows must be complete. Integration is recommended only if at least seven audio results score at least 4 on **all three** ratings and at least seven MIDI results need none/minor corrections. These are separate counts; the report exposes both. Otherwise revise the failing generation or transcription stage before app integration. Synthetic tests never populate human scores.

## References and limits

- [MusicGen input/output and GPU guidance](https://github.com/facebookresearch/audiocraft/blob/896ec7c47f5e5d1e5aa1e4b260c4405328bf009d/docs/MUSICGEN.md)
- [Model card and noncommercial weights license](https://github.com/facebookresearch/audiocraft/blob/896ec7c47f5e5d1e5aa1e4b260c4405328bf009d/model_cards/MUSICGEN_MODEL_CARD.md)
- [Colab availability and interactive-use restrictions](https://research.google.com/colaboratory/faq.html)
- [Current evaluation status](evaluation-report.md)

Keep all audio, trial request/result metadata, filled ratings and downloaded weights outside Git. `trial-data/` and `build/` are ignored. Commit only code, clean notebooks, tests and anonymized written findings.

### Recording directly in AI Trial

Choose a microphone and click **Record voice** inside AI Trial. Sing or hum for 0.5–10 seconds, then press **Stop recording** (or let the ten-second limit stop it). The new take is selected automatically; use **Listen to selected voice** to check it. You can also select an existing project voice recording from the dropdown.

These takes belong only to the trial screen and are cleared when it is reopened. Save trial files to retain a take before closing. Recording does not change Hum → MIDI or add anything to the Playlist. Choose an instrument and continue with **Save trial files → Open free Colab trial → Import generated WAV**. Recording does not start cloud generation or upload automatically.
