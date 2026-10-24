"""Synthetic trial-contract tests. No cloud access or human-quality claims."""
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import wave

TOOL = Path(sys.argv[1]).resolve()
ROOT = Path(__file__).resolve().parents[1]
NEURAL = "--neural" in sys.argv
sys.argv[1:] = []
sys.path.insert(0, str(ROOT / "experiments/audiocraft"))
import trial_runner as runner
import evaluate
import notebook_workflow as workflow


def fixture(samples):
    pcm = ''.join(f'{x & 65535:04x}' for x in samples)
    return ('HUM_TO_SING_PROJECT 6\ntempo_bpm 93.75\nticks_per_quarter 480\nroot_pitch_class 0\n'
            'scale_mode major\nmagnet_enabled 0\nrhythm_snap_enabled 0\nrhythm_snap_seconds 0.125\n'
            'metronome_enabled 0\ngrid_division 16\naudio_storage embedded_voice\nnote_count 0\n'
            'track_count 1\ntrack melody grand_piano 0\nregion_count 0\nlyrics "Private lyrics must not be exported"\n'
            f'voice_count 2\nvoice "Do not select" 0 8000 0 8000 {"1000" * 8000}\n'
            f'voice "Chosen" 4.25 8000 1 {len(samples)} {pcm}\nend\n')


class TrialTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.samples = [round(9000 * math.sin(i * math.tau * 220 / 8000)) for i in range(16000)]
        self.project = self.dir / "private.humproj"
        self.project.write_text(fixture(self.samples))

    def call(self, *args, success=True):
        result = subprocess.run([str(TOOL), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def prepare(self, output=None, extra=(), success=True):
        output = output or self.dir / "request"
        self.call("prepare", "--project", self.project, "--clip", "2", "--instrument", "flute",
                  "--output", output, *extra, success=success)
        return output

    def test_explicit_selection_pcm_prompt_and_no_project_mutation(self):
        before = hashlib.sha256(self.project.read_bytes()).hexdigest()
        out = self.prepare(extra=("--offset", ".25", "--duration", "1.5", "--style", "gentle", "--seed", "123"))
        request = runner.validate_request(out / "request.json", out / "guide.wav")
        self.assertEqual(request["duration_seconds"], 1.5)
        self.assertEqual(request["tempo_bpm"], 93.75)
        self.assertEqual(request["seed"], 123)
        self.assertEqual(request["prompt"], "Solo flute, expressive single-note melody, gentle phrasing, sparse unaccompanied performance, one melodic line, 93.75 BPM.")
        self.assertNotIn("Private", (out / "request.json").read_text())
        with wave.open(str(out / "guide.wav")) as w:
            self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate()), (1, 2, 8000))
            actual = struct.unpack("<" + "h" * w.getnframes(), w.readframes(w.getnframes()))
        self.assertEqual(list(actual), self.samples[2000:14000])
        self.assertEqual(hashlib.sha256(self.project.read_bytes()).hexdigest(), before)
        self.assertIn('2\t"Chosen"', self.call("list", "--project", self.project).stdout)

    def test_neutral_default_and_remaining_duration(self):
        out = self.prepare()
        request = runner.validate_request(out / "request.json", out / "guide.wav")
        self.assertEqual(request["duration_seconds"], 2)
        self.assertEqual(request["style"], "neutral")
        self.assertNotIn("gentle", request["prompt"])
        self.assertEqual(set(p.name for p in out.iterdir()), {"guide.wav", "request.json"})

    def test_all_instruments_and_styles(self):
        for instrument in runner.INSTRUMENTS:
            for style in runner.STYLES:
                out = self.dir / (instrument + style)
                self.call("prepare", "--project", self.project, "--clip", "2", "--instrument", instrument,
                          "--style", style, "--output", out)
                request = runner.validate_request(out / "request.json", out / "guide.wav")
                self.assertEqual(request["instrument"], instrument)
                self.assertEqual(request["style"], style)

    def test_invalid_arguments_create_no_output(self):
        for extra in (("--duration", "11"), ("--offset", "2"), ("--duration", "nan"),
                      ("--duration", "0.1"), ("--style", "unknown"), ("--seed", "-1"), ("--seed", "4294967296"),
                      ("--unknown", "x"), ("--offset", "-1"), ("--duration", "inf")):
            with self.subTest(extra=extra):
                self.prepare(extra=extra, success=False)
                self.assertFalse((self.dir / "request").exists())
        for clip in ("0", "3", "abc"):
            self.call("prepare", "--project", self.project, "--clip", clip, "--instrument", "piano",
                      "--output", self.dir / "bad", success=False)
        self.project.write_text("corrupt")
        self.prepare(success=False)

    def test_silence_and_existing_output_are_rejected(self):
        out = self.prepare()
        original = (out / "request.json").read_bytes()
        self.prepare(success=False)
        self.assertEqual((out / "request.json").read_bytes(), original)
        self.project.write_text(fixture([0] * 8000))
        self.prepare(self.dir / "silent", success=False)
        self.assertFalse((self.dir / "silent").exists())

    def test_request_contract_rejects_bad_or_mismatched_inputs(self):
        out = self.prepare()
        path = out / "request.json"
        original = json.loads(path.read_text())
        for key, value in (("model", "facebook/musicgen-small"), ("audio_file", "../secret.wav"),
                           ("duration_seconds", 10), ("duration_seconds", float('nan')), ("seed", True),
                           ("tempo_bpm", 999), ("prompt", ""), ("instrument", "drums")):
            changed = dict(original, **{key: value}); path.write_text(json.dumps(changed))
            with self.subTest(key=key), self.assertRaises(ValueError):
                runner.validate_request(path, out / "guide.wav")
        path.write_text(json.dumps(original))
        data = (out / "guide.wav").read_bytes()
        (out / "guide.wav").write_bytes(data[:-100])
        with self.assertRaises(ValueError): runner.validate_request(path, out / "guide.wav")

    def test_gpu_preflight_has_no_cpu_or_small_model_fallback(self):
        for available, memory in ((False, 16), (True, 8)):
            fake = SimpleNamespace(cuda=SimpleNamespace(is_available=lambda: available,
                get_device_properties=lambda _: SimpleNamespace(total_memory=memory * 1024 ** 3, name="test")))
            with self.assertRaises(RuntimeError): runner.gpu_preflight(fake)
        fake.cuda.get_device_properties = lambda _: SimpleNamespace(total_memory=15 * 1024 ** 3, name="T4")
        self.assertEqual(runner.gpu_preflight(fake)["name"], "T4")

    def test_notebook_is_clean_and_embedded_runner_matches(self):
        notebook = json.loads((ROOT / "experiments/audiocraft/voice_to_instrument_trial.ipynb").read_text())
        namespace = {}
        for cell in notebook["cells"]:
            if cell["cell_type"] == "code":
                self.assertEqual(cell["outputs"], [])
                self.assertIsNone(cell["execution_count"])
                source = ''.join(cell["source"])
                compile(source, "notebook", "exec")
                if source.startswith("RUNNER_SOURCE ="):
                    exec(source.splitlines()[0], namespace)
                if source.startswith("WORKFLOW_SOURCE ="):
                    exec(source.splitlines()[0], namespace)
                if source.startswith("CONSTRAINTS ="):
                    exec(source.splitlines()[0], namespace)
        self.assertEqual(namespace["RUNNER_SOURCE"], (ROOT / "experiments/audiocraft/trial_runner.py").read_text())
        self.assertEqual(namespace["WORKFLOW_SOURCE"], (ROOT / "experiments/audiocraft/notebook_workflow.py").read_text())
        self.assertEqual(namespace["CONSTRAINTS"], (ROOT / "experiments/audiocraft/constraints.txt").read_text())

    def test_notebook_progress_and_failure_do_not_fake_success(self):
        child = self.dir / "fake_runner.py"
        output = self.dir / "result"
        child.write_text("import sys,json,pathlib\nprint('HUM_PROGRESS '+json.dumps(dict(stage='generating',completed=2,total=4)),flush=True)\np=pathlib.Path(sys.argv[sys.argv.index('--output')+1]);p.mkdir();(p/'SUCCESS').touch()\n")
        events = []
        workflow.run_generation(sys.executable, child, "request", "guide", output, events.append)
        self.assertTrue(any(e['completed'] == 2 and e['total'] == 4 for e in events))
        self.assertEqual(events[-1]['stage'], 'complete')
        self.assertGreaterEqual(events[-1]['elapsed'], 0)
        child.write_text("raise SystemExit('No CUDA GPU')\n")
        events.clear()
        with self.assertRaisesRegex(RuntimeError, 'No CUDA GPU'):
            workflow.run_generation(sys.executable, child, "request", "guide", self.dir / "failed", events.append)
        self.assertFalse(any(e['stage'] == 'complete' for e in events))
        child.write_text("print('HUM_PROGRESS {broken')\n")
        with self.assertRaises(ValueError):
            workflow.run_generation(sys.executable, child, "request", "guide", self.dir / "bad", events.append)

    def test_notebook_interrupt_stops_child(self):
        child = self.dir / "slow.py"
        child.write_text("import time\nprint('loading',flush=True)\ntime.sleep(60)\n")
        def interrupt(event):
            raise KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt):
            workflow.run_generation(sys.executable, child, "request", "guide", self.dir / "out", interrupt)

    @unittest.skipUnless(shutil.which("node"), "Node not available for notebook browser transport simulation")
    def test_browser_download_progress_and_truncation(self):
        harness = r"""
const fs = require('fs');
const transfer = eval('(' + fs.readFileSync(0, 'utf8') + ')');
let clicked = 0, ended = 0;
global.document = {createElement: () => ({style: {}, append(){}, click(){clicked++;}}), body: {append(){}}};
global.setTimeout = () => {};
global.setInterval = () => 1;
global.clearInterval = () => {ended++;};
global.URL = {createObjectURL: () => 'blob:test', revokeObjectURL(){}};
global.google = {colab: {kernel: {accessAllowed:true, comms:{open:async () => ({send(){}, async *messages(){}})}}}};
const kernel = google.colab.kernel;
kernel.comms.open = async () => ({send(){}, messages: (async function*(){yield {buffers:[new Uint8Array(3)]}; yield {buffers:[new Uint8Array(2)]};})()});
(async () => {
 const result = await transfer('id','result.zip',5);
 if (result.bytes !== 5 || result.seconds < 0 || clicked !== 1 || ended !== 1) throw Error('invalid completed transfer');
 try {await transfer('id','result.zip',6); throw Error('accepted truncated transfer');}
 catch(e) {if (!e.message.includes('Download interrupted')) throw e;}
 if (clicked !== 1 || ended !== 2) throw Error('failed transfer offered a download or leaked timer');
})().catch(e => {console.error(e); process.exitCode=1;});
"""
        result = subprocess.run([shutil.which('node'), '-e', harness], input=workflow.DOWNLOAD_JS,
                                text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_result_download_stream_and_cleanup(self):
        archive = self.dir / "result.zip"
        archive.write_bytes(b'x' * 600000)
        targets, chunks, closed = {}, [], []
        manager = SimpleNamespace(register_target=lambda key, fn: targets.update({key: fn}),
                                  unregister_target=lambda key, fn: targets.pop(key))
        callback = {}
        comm = SimpleNamespace(send=lambda data, buffers: chunks.extend(buffers),
                               close=lambda: closed.append(True),
                               on_msg=lambda fn: callback.update(next=fn), on_close=lambda fn: None)
        def evaluate(js, timeout_sec):
            self.assertIn('result.zip', js)
            self.assertEqual(timeout_sec, 600)
            next(iter(targets.values()))(comm, None)
            while not closed: callback['next'](None)
            return dict(bytes=sum(map(len, chunks)), seconds=1)
        with patch.dict(sys.modules, {'google.colab': SimpleNamespace(output=SimpleNamespace(eval_js=evaluate)),
                                      'IPython': SimpleNamespace(get_ipython=lambda: SimpleNamespace(kernel=SimpleNamespace(comm_manager=manager)))}):
            result = workflow.download_result(archive)
        self.assertEqual(result['bytes'], 600000)
        self.assertEqual(b''.join(chunks), archive.read_bytes())
        self.assertFalse(targets)

    def test_evaluation_requires_nine_complete_real_ratings(self):
        template = ROOT / "experiments/audiocraft/evaluation-template.csv"
        self.assertEqual(evaluate.evaluate(template)["decision"], "not_evaluated")
        rows = list(csv.DictReader(template.open()))
        for i, row in enumerate(rows):
            for key in evaluate.FIELDS[2:5]: row[key] = "4" if i < 7 else "2"
            row.update(midi_corrections="minor" if i < 7 else "major", generation_seconds="30", correction_minutes="1")
        path = self.dir / "ratings.csv"
        def save():
            with path.open("w", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=evaluate.FIELDS); writer.writeheader(); writer.writerows(rows)
        save(); self.assertEqual(evaluate.evaluate(path)["decision"], "recommend_integration")
        rows[0]["midi_corrections"] = "major"
        save(); self.assertEqual(evaluate.evaluate(path)["decision"], "revise_approach")
        rows[0]["midi_corrections"] = ""
        save(); self.assertEqual(evaluate.evaluate(path)["decision"], "not_evaluated")
        rows[0]["instrument_accuracy"] = "6"
        save()
        with self.assertRaises(ValueError): evaluate.evaluate(path)

    @unittest.skipUnless(NEURAL, "Native neural runtime not configured")
    def test_synthetic_audio_to_real_midi_and_reference(self):
        request = self.prepare()
        out = self.dir / "transcription"
        self.call("transcribe", "--audio", request / "guide.wav", "--bpm", "93.75", "--instrument", "flute", "--output", out)
        midi = (out / "generated.mid").read_bytes()
        self.assertEqual(midi[:4], b'MThd')
        self.assertIn(b'MTrk', midi)
        self.assertIn(bytes([0xc0, 73]), midi)  # actual GM flute routing
        self.assertIn(bytes([0x90, 57]), midi)  # A3 from the synthetic 220-Hz tone
        notes = list(csv.DictReader((out / "notes.csv").open()))
        self.assertTrue(any(int(n["pitch"]) == 57 and float(n["duration_seconds"]) > 1.5 for n in notes))
        with wave.open(str(out / "midi-reference.wav")) as wav:
            self.assertGreater(wav.getnframes(), 32000)
        self.assertEqual(json.loads((out / "transcription.json").read_text())["quality"], "unrated")
        self.call("transcribe", "--audio", request / "guide.wav", "--bpm", "93.75", "--instrument", "flute", "--output", out, success=False)


if __name__ == "__main__": unittest.main()
