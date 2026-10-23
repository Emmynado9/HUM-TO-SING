"""Interactive MusicGen Melody trial. Imports ML libraries only after validation.
No server, upload client, fallback model, automatic retry or billing operation.
"""
import argparse
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import platform
import time
import wave

MODEL = "facebook/musicgen-melody"
INSTRUMENTS = ("piano", "acoustic-guitar", "flute")
STYLES = ("neutral", "gentle", "bright", "melancholic", "energetic")
AUDIOCRAFT_REVISION = "896ec7c47f5e5d1e5aa1e4b260c4405328bf009d"


def validate_request(request_path, guide_path):
    request_path, guide_path = Path(request_path), Path(guide_path)
    if request_path.stat().st_size > 8192 or guide_path.stat().st_size > 4_000_044:
        raise ValueError("Oversized trial input")
    request = json.loads(request_path.read_text(encoding="utf-8"))
    if not isinstance(request, dict):
        raise ValueError("Request must be an object")
    if type(request.get("schema_version")) is not int or request["schema_version"] != 1:
        raise ValueError("Unsupported request version")
    if request.get("model") != MODEL or request.get("audio_file") != "guide.wav":
        raise ValueError("Only MusicGen Melody with guide.wav is permitted")
    if request.get("instrument") not in INSTRUMENTS or request.get("style") not in STYLES:
        raise ValueError("Unknown instrument or style")
    for key, low, high in (("tempo_bpm", 20, 400), ("duration_seconds", .5, 10)):
        value = request.get(key)
        if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError("Invalid " + key)
    seed = request.get("seed")
    if type(seed) is not int or not 0 <= seed <= 0xFFFFFFFF:
        raise ValueError("Seed must be a 32-bit unsigned integer")
    prompt = request.get("prompt")
    if not isinstance(prompt, str) or not prompt.strip() or len(prompt) > 1000:
        raise ValueError("Missing or oversized prompt")
    with wave.open(str(guide_path), "rb") as wav:
        rate, frames = wav.getframerate(), wav.getnframes()
        if wav.getnchannels() != 1 or wav.getsampwidth() != 2 or not 8000 <= rate <= 192000:
            raise ValueError("Guide must be mono PCM16 WAV at 8–192 kHz")
        if not .5 <= frames / rate <= 10 or abs(frames / rate - request["duration_seconds"]) > 1 / rate:
            raise ValueError("Guide duration does not match request")
        pcm = wav.readframes(frames)
        if len(pcm) != frames * 2:
            raise ValueError("Truncated guide audio")
        import array
        import sys
        samples = array.array("h", pcm)
        if sys.byteorder != "little":
            samples.byteswap()
        if max(abs(x) for x in samples) <= 32:
            raise ValueError("Guide is effectively silent")
    return request


def gpu_preflight(torch):
    if not torch.cuda.is_available():
        raise RuntimeError("No CUDA GPU. Stop and request a free GPU runtime; no CPU/text-only fallback.")
    props = torch.cuda.get_device_properties(0)
    # A T4 reports ~15 GiB usable memory despite being marketed as 16 GB.
    if props.total_memory < 14 * 1024 ** 3:
        raise RuntimeError("Insufficient GPU memory for this trial. No paid upgrade will be attempted.")
    return {"name": props.name, "total_memory_bytes": props.total_memory}


def run(request_path, guide_path, output_dir):
    request = validate_request(request_path, guide_path)
    output = Path(output_dir)
    if output.exists():
        raise ValueError("Use a new output directory; previous results are never overwritten")
    import torch
    gpu = gpu_preflight(torch)
    import torchaudio
    from huggingface_hub import HfApi, snapshot_download
    from audiocraft.models import MusicGen

    # Resolve once, pin this run, and record the exact model snapshot used.
    revision = HfApi().model_info(MODEL).sha
    if not isinstance(revision, str) or len(revision) != 40 or any(c not in "0123456789abcdef" for c in revision):
        raise RuntimeError("Could not resolve an immutable model revision")
    snapshot = snapshot_download(MODEL, revision=revision,
                                 allow_patterns=["state_dict.bin", "compression_state_dict.bin"])
    load_started = time.monotonic()
    model = MusicGen.get_pretrained(snapshot, device="cuda")
    # Seed after initialization so loading the model does not consume the sampling seed.
    torch.manual_seed(request["seed"])
    torch.cuda.manual_seed_all(request["seed"])
    model.set_generation_params(duration=request["duration_seconds"], use_sampling=True,
                                top_k=250, top_p=0.0, temperature=1.0, cfg_coef=3.0)
    melody, rate = torchaudio.load(str(guide_path))
    load_seconds = time.monotonic() - load_started
    torch.cuda.synchronize()
    started = time.monotonic()
    with torch.inference_mode():
        result = model.generate_with_chroma([request["prompt"]], melody.unsqueeze(0), rate,
                                            progress=True)
    torch.cuda.synchronize()
    elapsed = time.monotonic() - started
    result = result[0].detach().cpu()
    if result.ndim != 2 or result.shape[0] != 1 or not torch.isfinite(result).all():
        raise RuntimeError("Unexpected or non-finite model output")
    expected = round(request["duration_seconds"] * model.sample_rate)
    if result.shape[-1] < expected - model.sample_rate // 10:
        raise RuntimeError("Generated output is unexpectedly short")
    result = result[:, :expected]
    peak = result.abs().max().item()
    if peak <= 1e-5:
        raise RuntimeError("Model returned silence; no successful result recorded")
    gain = min(1.0, .95 / peak)
    result *= gain
    packages = {dist.metadata["Name"]: dist.version for dist in importlib.metadata.distributions() if dist.metadata["Name"]}
    actual_direct = importlib.metadata.distribution("audiocraft").read_text("direct_url.json")
    metadata = dict(request)
    metadata.update(model_revision=revision, audiocraft_revision=AUDIOCRAFT_REVISION,
                    audiocraft_install=json.loads(actual_direct) if actual_direct else None,
                    guide_sha256=hashlib.sha256(Path(guide_path).read_bytes()).hexdigest(),
                    generation_seconds=elapsed, load_seconds=load_seconds, output_gain=gain,
                    output_sample_rate=model.sample_rate, output_frames=result.shape[-1],
                    gpu=gpu, quality="unrated", sampling={"top_k": 250, "temperature": 1.0, "cfg_coef": 3.0})
    output.mkdir(parents=True, exist_ok=False)
    try:
        torchaudio.save(str(output / "generated.wav"), result, model.sample_rate,
                       encoding="PCM_S", bits_per_sample=16)
        (output / "result.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        (output / "environment.json").write_text(json.dumps({"python": platform.python_version(), "packages": packages}, indent=2) + "\n")
        (output / "SUCCESS").write_text("Generation completed; listening and MIDI evaluation still required.\n")
    except Exception:
        # Keep any partial output marked incomplete; never publish it as successful.
        (output / "INCOMPLETE").write_text("Output writing failed. Use a fresh result directory.\n")
        raise
    return metadata


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--request", required=True)
    parser.add_argument("--guide", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        run(args.request, args.guide, args.output)
    except Exception as error:
        raise SystemExit("Trial stopped (no automatic retry or paid fallback): " + str(error))
