"""Interactive Colab helpers; no HTTP service or desktop upload endpoint."""
import json
from pathlib import Path
import queue
import subprocess
import threading
import time
import uuid


def run_generation(python, runner, request, guide, output, on_progress):
    """Forward actual model steps, keep elapsed time live, and stop on interruption."""
    started = time.monotonic()
    process = subprocess.Popen([str(python), '-u', str(runner), '--request', str(request),
                                '--guide', str(guide), '--output', str(output), '--progress-json'],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines = queue.Queue()
    def read():
        try:
            for line in process.stdout:
                lines.put(line)
        finally:
            lines.put(None)
    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    state = dict(stage='loading', completed=0, total=0)
    tail = []
    try:
        while True:
            try:
                line = lines.get(timeout=.5)
            except queue.Empty:
                line = ''
            if line is None:
                break
            if line.startswith('HUM_PROGRESS '):
                event = json.loads(line[len('HUM_PROGRESS '):])
                if (event.get('stage') not in ('loading', 'generating', 'saving', 'complete')
                        or type(event.get('completed')) is not int or type(event.get('total')) is not int
                        or not 0 <= event['completed'] <= event['total']):
                    raise ValueError('Invalid model progress event')
                state = event
            elif line:
                tail.append(line.rstrip()[-2000:])
                tail = tail[-12:]
            on_progress(dict(state, elapsed=time.monotonic() - started))
        code = process.wait()
        if code != 0 or not (Path(output) / 'SUCCESS').is_file():
            raise RuntimeError('Generation stopped; no result will be downloaded.\n' + '\n'.join(tail))
        on_progress(dict(stage='complete', completed=1, total=1, elapsed=time.monotonic() - started))
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        reader.join(timeout=5)
        process.stdout.close()


# Transfer result bytes over the active notebook kernel channel. No tunnel/server.
# Completion means bytes reached the browser; the user's disk-save dialog is separate.
DOWNLOAD_JS = r'''async (channelId, filename, size) => {
  const box = document.createElement('div');
  const title = document.createElement('div');
  const meter = document.createElement('progress');
  meter.max = size; meter.value = 0; meter.style.width = '100%';
  box.append(title, meter); document.body.append(box);
  const began = performance.now();
  let received = 0;
  const update = () => {
    const seconds = (performance.now() - began) / 1000;
    const eta = received > 0 ? seconds * (size - received) / received : null;
    title.textContent = `Downloading result: ${Math.floor(100 * received / size)}% · ${seconds.toFixed(1)} s elapsed` +
      (eta === null ? '' : ` · ~${eta.toFixed(1)} s remaining`);
  };
  const clock = setInterval(update, 100);
  let channel;
  try {
    if (!google.colab.kernel.accessAllowed) throw new Error('Notebook access is not allowed');
    channel = await google.colab.kernel.comms.open(channelId);
    const pieces = [];
    channel.send({});
    for await (const packet of channel.messages) {
      for (const piece of packet.buffers || []) {
        received += piece.byteLength;
        if (received > size) throw new Error('Unexpected download size');
        pieces.push(piece);
      }
      meter.value = received; update(); channel.send({});
    }
    if (received !== size) throw new Error('Download interrupted; rerun the download cell');
    const url = URL.createObjectURL(new Blob(pieces, {type: 'application/zip'}));
    const link = document.createElement('a');
    link.href = url; link.download = filename; link.textContent = 'Save result ZIP again';
    box.append(link); link.click();
    setTimeout(() => URL.revokeObjectURL(url), 300000);
    const seconds = (performance.now() - began) / 1000;
    title.textContent = `Result transferred to browser in ${seconds.toFixed(1)} s. Check Downloads or the save dialog.`;
    return {bytes: received, seconds};
  } catch (error) {
    title.textContent = `Download failed: ${error.message}. Rerun Download again below; do not regenerate.`;
    throw error;
  } finally {
    clearInterval(clock);
    if (channel && typeof channel.close === 'function') channel.close();
  }
}'''


def download_result(path):
    from google.colab import output
    from IPython import get_ipython
    path = Path(path)
    size = path.stat().st_size
    if not 0 < size <= 32 * 1024 * 1024:
        raise ValueError('Invalid result archive size')
    manager = get_ipython().kernel.comm_manager
    key = 'hum_result_' + uuid.uuid4().hex
    streams = []
    def opened(comm, message):
        stream = path.open('rb')
        streams.append(stream)
        def next_chunk(message):
            block = stream.read(256 * 1024)
            if block:
                comm.send({}, buffers=[block])
            else:
                stream.close()
                comm.close()
        comm.on_msg(next_chunk)
        comm.on_close(lambda message: stream.close())
    manager.register_target(key, opened)
    try:
        return output.eval_js('(' + DOWNLOAD_JS + ')(' + ','.join(map(json.dumps, (key, path.name, size))) + ')', timeout_sec=600)
    finally:
        manager.unregister_target(key, opened)
        for stream in streams:
            stream.close()
