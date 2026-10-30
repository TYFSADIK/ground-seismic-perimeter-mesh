"""Tune classifier thresholds from labelled feature logs (grid search maximising macro-F1).

CSV columns: label, rms, low, mid, high, kurtosis, cadence
Prints thresholds to paste into firmware Thresholds{}.
"""
import csv
import itertools
import sys


def classify(r, th):
    if r["rms"] < th["noise_gate_rms"]:
        return "quiet"
    if r["cadence"] >= th["foot_cadence"] and r["kurtosis"] >= th["foot_kurtosis"]:
        return "footsteps"
    if r["low"] >= th["vehicle_low_frac"]:
        return "vehicle"
    if r["high"] >= th["wind_high_frac"]:
        return "wind"
    return "unknown"


def macro_f1(rows, th):
    labels = sorted({r["label"] for r in rows})
    f1s = []
    for L in labels:
        tp = sum(1 for r in rows if r["label"] == L and classify(r, th) == L)
        fp = sum(1 for r in rows if r["label"] != L and classify(r, th) == L)
        fn = sum(1 for r in rows if r["label"] == L and classify(r, th) != L)
        p = tp / (tp + fp) if tp + fp else 0
        rc = tp / (tp + fn) if tp + fn else 0
        f1s.append(2 * p * rc / (p + rc) if p + rc else 0)
    return sum(f1s) / len(f1s)


GRID = {"noise_gate_rms": [0.001, 0.002, 0.004], "vehicle_low_frac": [0.5, 0.6, 0.7],
        "foot_cadence": [0.3, 0.45, 0.6], "foot_kurtosis": [3.5, 4.0, 5.0], "wind_high_frac": [0.3, 0.4, 0.5]}


def tune(rows):
    best = (None, -1)
    for combo in itertools.product(*GRID.values()):
        th = dict(zip(GRID, combo))
        s = macro_f1(rows, th)
        if s > best[1]:
            best = (th, s)
    return best


if __name__ == "__main__":
    rows = [{k: (v if k == "label" else float(v)) for k, v in r.items()} for r in csv.DictReader(open(sys.argv[1]))]
    th, s = tune(rows)
    print(f"macro-F1 {s:.3f}")
    for k, v in th.items():
        print(f"  {k} = {v}f;")
