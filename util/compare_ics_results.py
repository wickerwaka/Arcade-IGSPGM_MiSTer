#!/usr/bin/env python3
"""Diff two ICS2115 conformance-test JSONL files (sim vs hw, or run vs run).

    python3 util/compare_ics_results.py results/sim.jsonl results/hw.jsonl

Records are keyed by (test_id, params); observation values are compared
exactly.  Exit code 0 = identical, 1 = differences found.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


# Per-test-id-prefix numeric tolerances: timer measurements depend on timer
# phase relative to the measurement window, so counts may differ slightly
# between runs/targets without indicating a real divergence.
TOLERANCES: list[tuple[str, str, float]] = [
    ("t_tmr.", "count", 8),
    ("t_tmr.", "frames", 3),
    ("t_tmr.", "other_timer_count", 1),
    ("t_tmr.", "spurious", 2),
    ("t_voice.loop_end_irqs", "z80_osc_irqs", 1),
    ("t_octl.reprogram_gate", "osc_acc_gated", 4096),  # free-running osc accumulator sample
    ("t_voice.oneshot_end_irq", "z80_osc_irqs", 1),
]


# Relative tolerances (fraction of the larger magnitude) for audio metrics:
# analog of timing tolerance — capture windows land at different phases.
REL_TOLERANCES: list[tuple[str, str, float]] = [
    ("t_aud.", "rms_l", 0.15),
    ("t_aud.sys_gate", "rms_l", 0.15),
    ("t_aud.", "rms_r", 0.15),
    ("t_aud.", "peak", 0.20),
    ("t_aud.", "zcr", 0.10),
    ("t_aud.", "ramp_samples", 0.15),
    ("t_rate.", "spv", 0.15),
    ("t_rate.", "zcr_ratio_vs31", 0.15),
    ("t_rate.", "zcr", 0.12),
    ("t_rate.", "vblanks", 0.20),
]


def field_rel_tolerance(test_id: str, field: str) -> float:
    for prefix, name, tol in REL_TOLERANCES:
        if test_id.startswith(prefix) and field == name:
            return tol
    return 0


def field_tolerance(test_id: str, field: str) -> float:
    for prefix, name, tol in TOLERANCES:
        if test_id.startswith(prefix) and field == name:
            return tol
    return 0


def obs_equal(test_id: str, obs_a: dict, obs_b: dict) -> bool:
    if set(obs_a) != set(obs_b):
        return False
    for field, va in obs_a.items():
        vb = obs_b[field]
        tol = field_tolerance(test_id, field)
        rel = field_rel_tolerance(test_id, field)
        if isinstance(va, (int, float)) and isinstance(vb, (int, float)) and (tol or rel):
            bound = max(tol, rel * max(abs(va), abs(vb)))
            if abs(va - vb) > bound:
                return False
        elif va != vb:
            return False
    return True


def load(path: Path) -> dict[tuple[str, str], dict]:
    records = {}
    for lineno, line in enumerate(path.read_text().splitlines(), start=1):
        if not line.strip():
            continue
        rec = json.loads(line)
        if "_meta" in rec:
            continue
        key = (rec["test_id"], json.dumps(rec["params"], sort_keys=True))
        if key in records:
            print(f"warning: duplicate record key {key} at {path}:{lineno}", file=sys.stderr)
        records[key] = rec["obs"]
    return records


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("file_a", type=Path)
    ap.add_argument("file_b", type=Path)
    ap.add_argument("--verbose", action="store_true", help="print matching counts per test_id")
    args = ap.parse_args()

    a = load(args.file_a)
    b = load(args.file_b)

    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    mismatched = []
    matched_by_test: dict[str, int] = {}

    for key in sorted(set(a) & set(b)):
        if not obs_equal(key[0], a[key], b[key]):
            mismatched.append(key)
        else:
            matched_by_test[key[0]] = matched_by_test.get(key[0], 0) + 1

    for key in only_a:
        print(f"ONLY-A {key[0]} {key[1]}")
    for key in only_b:
        print(f"ONLY-B {key[0]} {key[1]}")
    for key in mismatched:
        print(f"MISMATCH {key[0]} {key[1]}")
        obs_a, obs_b = a[key], b[key]
        fields = sorted(set(obs_a) | set(obs_b))
        for field in fields:
            va, vb = obs_a.get(field), obs_b.get(field)
            if va != vb:
                print(f"    {field}: A={va!r} B={vb!r}")

    if args.verbose:
        for test_id in sorted(matched_by_test):
            print(f"match {test_id}: {matched_by_test[test_id]}")

    total_common = len(set(a) & set(b))
    print(f"{total_common - len(mismatched)}/{total_common} common records match; "
          f"{len(mismatched)} mismatched, {len(only_a)} only in A, {len(only_b)} only in B")
    return 1 if (mismatched or only_a or only_b) else 0


if __name__ == "__main__":
    raise SystemExit(main())
