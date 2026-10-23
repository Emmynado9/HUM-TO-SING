"""Validate the nine-result listening sheet; never infer human scores from audio."""
import csv
import json
from pathlib import Path
import sys

EXCERPTS = ("sustain", "repeated-rests", "pitch-jumps")
INSTRUMENTS = ("piano", "acoustic-guitar", "flute")
FIELDS = ("excerpt", "instrument", "instrument_accuracy", "melodic_relevance", "musical_usefulness",
          "midi_corrections", "generation_seconds", "correction_minutes", "notes")


def evaluate(path):
    with Path(path).open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        if tuple(reader.fieldnames or ()) != FIELDS:
            raise ValueError("Unexpected evaluation columns")
        rows = list(reader)
    expected = {(a, b) for a in EXCERPTS for b in INSTRUMENTS}
    keys = [(r["excerpt"], r["instrument"]) for r in rows]
    if len(keys) != 9 or set(keys) != expected:
        raise ValueError("Exactly nine distinct excerpt/instrument combinations are required")
    audio_pass, midi_pass, completed = 0, 0, 0
    for row in rows:
        scores = [row[x].strip() for x in FIELDS[2:5]]
        midi = row["midi_corrections"].strip()
        for value in scores:
            if value and value not in ("1", "2", "3", "4", "5"):
                raise ValueError("Listening scores must be integers from 1 to 5")
        if midi not in ("", "none", "minor", "major", "unusable"):
            raise ValueError("MIDI corrections must be none, minor, major or unusable")
        for field in ("generation_seconds", "correction_minutes"):
            value = row[field].strip()
            if value:
                import math
                if not math.isfinite(float(value)) or float(value) < 0:
                    raise ValueError("Invalid timing value")
        if all(scores) and midi and row["generation_seconds"].strip() and row["correction_minutes"].strip():
            completed += 1
            audio_pass += all(int(x) >= 4 for x in scores)
            midi_pass += midi in ("none", "minor")
    decision = "not_evaluated" if completed != 9 else "recommend_integration" if audio_pass >= 7 and midi_pass >= 7 else "revise_approach"
    return {"completed": completed, "total": 9, "audio_pass": audio_pass, "midi_pass": midi_pass, "decision": decision}


if __name__ == "__main__":
    try:
        print(json.dumps(evaluate(sys.argv[1]), indent=2))
    except (ValueError, OSError, IndexError) as error:
        raise SystemExit("Evaluation stopped: " + str(error))
