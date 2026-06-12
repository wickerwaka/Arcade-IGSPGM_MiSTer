#!/usr/bin/env python3
"""Analyze ICS2115 VMode/VIncr volume-ramp captures.

Input CSV rows are:

    vmode,vincr,sample0,sample1,...

The capture used a constant input sample of -32768 and left channel output, so
for the measured hardware volume table the instantaneous gain is usually
approximately -sample.  This script streams the large CSV and emits compact
summaries that are easier to inspect and plot.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Iterable


PAN_ATTEN = [
    4096, 508, 364, 304,
    248, 200, 168, 140,
    116, 96, 76, 56,
    40, 28, 12, 0,
]


def volume_lut(i: int) -> int:
    if i <= 0:
        return 0
    if i > 4095:
        i = 4095
    exp = i >> 8
    mant = i & 0xFF
    if exp == 0:
        return mant >> 7
    return (((0x100 | mant) << (exp - 1)) + 0xFF) >> 8


# Gain -> first/min volume table index and last/max index.  The table has many
# repeated output values, especially near zero, so both are useful.
GAIN_TO_MIN_INDEX: dict[int, int] = {}
GAIN_TO_MAX_INDEX: dict[int, int] = {}
for _i in range(4096):
    _g = volume_lut(_i)
    GAIN_TO_MIN_INDEX.setdefault(_g, _i)
    GAIN_TO_MAX_INDEX[_g] = _i


def nearest_volume_index(gain: int) -> int:
    """Return a plausible volume-table index for a measured gain."""
    if gain in GAIN_TO_MAX_INDEX:
        return GAIN_TO_MAX_INDEX[gain]
    # Should be rare.  Keep simple; only 4096 table entries.
    return min(range(4096), key=lambda i: abs(volume_lut(i) - gain))


def parse_line(line: str) -> tuple[int, int, list[int]]:
    values = [int(x) for x in line.rstrip("\n").split(",")]
    if len(values) < 3:
        raise ValueError("expected vmode,vincr,samples...")
    return values[0], values[1], values[2:]


def summarize_samples(vmode: int, vincr: int, samples: list[int]) -> dict:
    gains = [max(0, -s) for s in samples]
    n = len(gains)

    transitions: list[int] = []
    reset_indices: list[int] = []
    positive_indices: list[int] = []
    max_gain = 0
    max_gain_index = None

    prev = gains[0] if gains else 0
    if prev > 0:
        positive_indices.append(0)
    for i, gain in enumerate(gains):
        if gain > max_gain:
            max_gain = gain
            max_gain_index = i
        if i == 0:
            continue
        if gain != prev:
            transitions.append(i)
            # Reset/wrap: substantial downward jump.  Near zero, table repeats
            # make tiny downward changes uninteresting, so require either a
            # return to zero or a large relative drop.
            if prev > 0 and gain < prev and (gain == 0 or prev - gain > max(8, prev // 4)):
                reset_indices.append(i)
        if gain > 0:
            positive_indices.append(i)
        prev = gain

    transition_deltas = [b - a for a, b in zip(transitions, transitions[1:])]
    reset_deltas = [b - a for a, b in zip(reset_indices, reset_indices[1:])]
    first_positive = positive_indices[0] if positive_indices else None
    last_positive = positive_indices[-1] if positive_indices else None
    unique_gain_count = len(set(gains))
    max_vol_index = nearest_volume_index(max_gain)

    if len(reset_indices) >= 2:
        period = round(sum(reset_deltas) / len(reset_deltas), 6)
    else:
        period = None

    active_span = None
    if first_positive is not None and last_positive is not None:
        active_span = last_positive - first_positive + 1

    if max_gain == 0:
        classification = "silent_or_static_zero"
    elif len(reset_indices) >= 1:
        classification = "wraps"
    elif unique_gain_count <= 2:
        classification = "nearly_static"
    else:
        classification = "ramps_no_wrap"

    # Average observed table-index movement per output transition and per
    # sample.  For rows that do not wrap, this is a lower-bound/partial-window
    # estimate.
    approx_index_rate = None
    if active_span and active_span > 1:
        approx_index_rate = max_vol_index / active_span

    return {
        "vmode": vmode,
        "vincr": vincr,
        "sample_count": n,
        "classification": classification,
        "first_positive": first_positive,
        "last_positive": last_positive,
        "active_span": active_span,
        "initial_gain": gains[0] if gains else 0,
        "final_gain": gains[-1] if gains else 0,
        "max_gain": max_gain,
        "max_gain_index": max_gain_index,
        "max_vol_index_est": max_vol_index,
        "unique_gain_count": unique_gain_count,
        "transition_count": len(transitions),
        "first_transitions": transitions[:16],
        "first_transition_deltas": transition_deltas[:16],
        "reset_count": len(reset_indices),
        "first_resets": reset_indices[:16],
        "reset_period_est": period,
        "approx_index_rate": approx_index_rate,
    }


def signature(summary: dict) -> tuple:
    """Coarse behavior signature used for vmode/bit relevance grouping."""
    period = summary["reset_period_est"]
    period_bucket = None if period is None else round(period, 3)
    return (
        summary["classification"],
        summary["max_gain"],
        summary["unique_gain_count"],
        summary["transition_count"],
        summary["reset_count"],
        period_bucket,
        tuple(summary["first_transition_deltas"][:8]),
        tuple(summary["first_resets"][:4]),
    )


def write_summary_csv(path: Path, summaries: Iterable[dict]) -> None:
    fields = [
        "vmode", "vincr", "sample_count", "classification",
        "first_positive", "last_positive", "active_span",
        "initial_gain", "final_gain", "max_gain", "max_gain_index",
        "max_vol_index_est", "unique_gain_count", "transition_count",
        "reset_count", "reset_period_est", "approx_index_rate",
        "first_transitions", "first_transition_deltas", "first_resets",
    ]
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for s in summaries:
            row = {k: s.get(k) for k in fields}
            for k in ("first_transitions", "first_transition_deltas", "first_resets"):
                row[k] = " ".join(str(x) for x in row[k])
            w.writerow(row)


def analyze(input_path: Path, out_prefix: Path, *, limit: int | None = None) -> dict:
    summaries: list[dict] = []
    by_vmode: dict[int, list[dict]] = defaultdict(list)
    by_vincr: dict[int, list[dict]] = defaultdict(list)

    with input_path.open("r", encoding="utf-8", newline="") as f:
        for row_no, line in enumerate(f, 1):
            if limit is not None and row_no > limit:
                break
            vmode, vincr, samples = parse_line(line)
            s = summarize_samples(vmode, vincr, samples)
            summaries.append(s)
            by_vmode[vmode].append(s)
            by_vincr[vincr].append(s)
            if row_no % 256 == 0:
                print(f"processed {row_no} rows...", flush=True)

    out_prefix.parent.mkdir(parents=True, exist_ok=True)
    write_summary_csv(out_prefix.with_suffix(".summary.csv"), summaries)

    # Compare VMode behavior.  For each vincr, record which vmodes share the
    # same signature.  This quickly shows whether only low bits matter.
    vmode_signature_by_vincr: dict[int, dict[int, tuple]] = defaultdict(dict)
    for s in summaries:
        vmode_signature_by_vincr[s["vincr"]][s["vmode"]] = signature(s)

    vmode_values = sorted(by_vmode)
    bit_report = []
    for bit in range(8):
        comparable = 0
        same = 0
        different_examples = []
        for vincr, sigs in sorted(vmode_signature_by_vincr.items()):
            for v in vmode_values:
                paired = v ^ (1 << bit)
                if v < paired and paired in sigs and v in sigs:
                    comparable += 1
                    if sigs[v] == sigs[paired]:
                        same += 1
                    elif len(different_examples) < 8:
                        different_examples.append({"vincr": vincr, "a": v, "b": paired})
        bit_report.append({
            "bit": bit,
            "comparable_pairs": comparable,
            "same_pairs": same,
            "different_pairs": comparable - same,
            "different_examples": different_examples,
        })

    # Group whole vmode rows by their sequence of per-vincr signatures.
    whole_mode_groups: dict[str, list[int]] = defaultdict(list)
    whole_mode_group_keys: list[tuple] = []
    for vmode in vmode_values:
        key = tuple(signature(s) for s in sorted(by_vmode[vmode], key=lambda x: x["vincr"]))
        try:
            idx = whole_mode_group_keys.index(key)
        except ValueError:
            whole_mode_group_keys.append(key)
            idx = len(whole_mode_group_keys) - 1
        whole_mode_groups[str(idx)].append(vmode)

    report = {
        "input": str(input_path),
        "rows": len(summaries),
        "vmodes": vmode_values,
        "vincr_min": min(by_vincr) if by_vincr else None,
        "vincr_max": max(by_vincr) if by_vincr else None,
        "class_counts": dict(sorted((c, sum(1 for s in summaries if s["classification"] == c)) for c in {s["classification"] for s in summaries})),
        "vmode_bit_relevance": bit_report,
        "whole_vmode_signature_groups": dict(whole_mode_groups),
    }
    out_prefix.with_suffix(".report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description="Summarize large vol_incr.csv captures")
    ap.add_argument("csv", nargs="?", default="vol_incr.csv", help="input CSV path")
    ap.add_argument("--out-prefix", default="audio_tests/vol_incr", help="output prefix without suffix")
    ap.add_argument("--limit", type=int, help="limit input rows for quick testing")
    args = ap.parse_args()

    report = analyze(Path(args.csv), Path(args.out_prefix), limit=args.limit)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
