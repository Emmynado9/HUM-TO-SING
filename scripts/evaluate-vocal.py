#!/usr/bin/env python3
import csv, json, math, subprocess, statistics
from pathlib import Path
import argparse
parser=argparse.ArgumentParser(description='Evaluate local Vocadito WAV/annotation files; never uploads audio.')
parser.add_argument('--dataset', type=Path, required=True)
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args=parser.parse_args()
root=args.dataset.resolve()
exe=args.executable.resolve()
output=args.output.resolve()
output.mkdir(parents=True,exist_ok=True)
def maximum_pairs(ref, pred, onset=.05, pitch=.5):
    edges=[]
    for r in ref:
        choices=[i for i,p in enumerate(pred) if abs(r['start']-p['start'])<=onset and abs(r['pitch']-p['pitch'])<=pitch]
        edges.append(sorted(choices,key=lambda i:abs(ref[len(edges)]['start']-pred[i]['start'])))
    owner={}
    def visit(r,seen):
        for p in edges[r]:
            if p in seen: continue
            seen.add(p)
            if p not in owner or visit(owner[p],seen): owner[p]=r; return True
        return False
    for r in range(len(ref)): visit(r,set())
    return [(r,p) for p,r in owner.items()]
rows=[]
for sample in [1,2,3]:
    reference=[]
    with (root/f'Annotations/Notes/vocadito_{sample}_notesA1.csv').open() as f:
        for t,hz,d in csv.reader(f):
            reference.append({'start':float(t),'pitch':69+12*math.log2(float(hz)/440),'duration':float(d)})
    for mode in ['natural','clean','tight','yin']:
        run=subprocess.run([str(exe),str(root/f'Audio/vocadito_{sample}.wav'),mode],text=True,capture_output=True,check=True)
        (output/f'{sample}-{mode}.csv').write_text(run.stdout)
        (output/f'{sample}-{mode}.log').write_text(run.stderr)
        notes=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(run.stdout.splitlines())]
        pairs=maximum_pairs(reference,notes)
        onset_pairs=maximum_pairs(reference,notes,onset=.15,pitch=128)
        stats={'sample':sample,'mode':mode,'reference_notes':len(reference),'predicted_notes':len(notes),'matched_notes_50ms_50c':len(pairs),
               'precision':len(pairs)/len(notes) if notes else 0,'recall':len(pairs)/len(reference),
               'f1':2*len(pairs)/(len(notes)+len(reference)),'processing':run.stderr.strip()}
        if pairs:
            stats.update({'onset_mae_ms':1000*statistics.mean(abs(reference[r]['start']-notes[p]['start']) for r,p in pairs),
                          'duration_mae_ms':1000*statistics.mean(abs(reference[r]['duration']-notes[p]['duration']) for r,p in pairs)})
        if onset_pairs:
            stats.update({'onset_pairs_150ms':len(onset_pairs),'pitch_mae_semitones_on_onset_pairs':statistics.mean(abs(reference[r]['pitch']-notes[p]['pitch']) for r,p in onset_pairs)})
        rows.append(stats); print(json.dumps(stats),flush=True)
(output/'metrics.json').write_text(json.dumps(rows,indent=2)+'\n')
