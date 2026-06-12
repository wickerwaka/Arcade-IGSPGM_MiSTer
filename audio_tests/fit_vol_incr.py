#!/usr/bin/env python3
"""Fit candidate VMode/VIncr volume-rate formulas from vol_incr summaries."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def load_rows(path: Path) -> list[dict]:
    rows = []
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            row = dict(r)
            row["vmode"] = int(row["vmode"])
            row["vincr"] = int(row["vincr"])
            row["period"] = float(row["reset_period_est"]) if row["reset_period_est"] else None
            row["reset_count"] = int(row["reset_count"])
            row["transition_count"] = int(row["transition_count"])
            row["max_gain"] = int(row["max_gain"])
            row["max_vol_index_est"] = int(row["max_vol_index_est"])
            row["approx_index_rate"] = float(row["approx_index_rate"]) if row["approx_index_rate"] else None
            rows.append(row)
    return rows


def odd_float_step(vincr: int) -> float:
    """Candidate floating-ish rate used by odd VMode rows.

    Let x = vincr[6:0].  The top two bits select a power-of-two scale and the
    low five bits are a mantissa with an implicit 0x20.  The exp=0 case is a
    half-scale range, giving continuity into exp=1.
    """
    x = vincr & 0x7F
    exp = x >> 5
    mant = x & 0x1F
    if exp == 0:
        return (0x20 + mant) / 2.0
    return float((0x20 + mant) << (exp - 1))


def even_linear_step(vincr: int) -> float:
    return float(vincr)


def slow_mod0_step(vincr: int) -> float:
    """Candidate very-slow rate for VMode values 0,4,8,12.

    The longer vmode=0 capture shows no movement for vincr <= 223 and a period
    of approximately 4177920 / (vincr - 192) for vincr 224..255.  Expressed in
    the same abstract rate units as mode 2, this is (0x20 + mantissa) / 64.
    """
    if (vincr >> 5) != 0x07:
        return 0.0
    return float(0x20 + (vincr & 0x1F)) / 64.0


def candidate_period(vmode: int, vincr: int) -> float | None:
    if vincr == 0:
        return None
    # Cleanly measured group: modes 2,6,10,14.
    if (vmode & 0x03) == 0x02:
        return 65280.0 / even_linear_step(vincr)
    # Odd modes: candidate compact-float rate with a 4x larger full-scale base.
    if vmode & 0x01:
        return 261120.0 / odd_float_step(vincr)
    # Modes 0,4,8,12: confirmed by longer vmode=0 capture for vincr 224..255.
    if (vmode & 0x03) == 0x00:
        step = slow_mod0_step(vincr)
        return None if step == 0 else 65280.0 / step
    return None


def summarize_fit(rows: list[dict]) -> dict:
    by_vmode: dict[int, list[dict]] = defaultdict(list)
    for row in rows:
        by_vmode[row["vmode"]].append(row)

    mode_reports = []
    for vmode in sorted(by_vmode):
        errors = []
        examples = []
        used = 0
        for row in by_vmode[vmode]:
            if row["period"] is None:
                continue
            pred = candidate_period(vmode, row["vincr"])
            if pred is None:
                continue
            err = row["period"] - pred
            rel = err / pred if pred else 0.0
            errors.append((err, rel))
            used += 1
            if len(examples) < 12:
                examples.append({
                    "vincr": row["vincr"],
                    "observed": row["period"],
                    "predicted": round(pred, 6),
                    "error": round(err, 6),
                })
        if errors:
            abs_err = [abs(e) for e, _ in errors]
            abs_rel = [abs(r) for _, r in errors]
            mode_reports.append({
                "vmode": vmode,
                "used_period_rows": used,
                "mean_abs_error_samples": statistics.mean(abs_err),
                "max_abs_error_samples": max(abs_err),
                "mean_abs_rel_error": statistics.mean(abs_rel),
                "max_abs_rel_error": max(abs_rel),
                "examples": examples,
            })
        else:
            mode_reports.append({"vmode": vmode, "used_period_rows": 0})

    return {"mode_reports": mode_reports}


def print_human(rows: list[dict], report: dict) -> None:
    print("Candidate formulas")
    print("  VMode & 3 == 2:")
    print("    period_samples ~= 65280 / VIncr")
    print("    equivalently one full 0xff00 accumulator ramp at +VIncr per sample")
    print("  VMode odd:")
    print("    x = VIncr & 0x7f")
    print("    exp = x >> 5, mant = x & 0x1f")
    print("    step = (0x20 + mant) / 2        if exp == 0")
    print("    step = (0x20 + mant) << (exp-1) if exp != 0")
    print("    period_samples ~= 261120 / step")
    print("  VMode & 3 == 0:")
    print("    inferred from no-wrap high-VIncr rows only")
    print("    if VIncr[7:5] == 7: step ~= (0x20 + (VIncr & 0x1f)) / 64")
    print("    predicted period_samples ~= 4177920 / (0x20 + (VIncr & 0x1f))")
    print("    lower VIncr exponent groups did not visibly move in this capture")
    print()

    print("Fit quality for rows with >=2 detected resets")
    for m in report["mode_reports"]:
        v = m["vmode"]
        if not m.get("used_period_rows"):
            print(f"  vmode {v:2d}: no fitted period rows")
            continue
        print(
            f"  vmode {v:2d}: n={m['used_period_rows']:3d} "
            f"mean_abs_err={m['mean_abs_error_samples']:.3f} samples "
            f"max_abs_err={m['max_abs_error_samples']:.3f} "
            f"mean_rel={m['mean_abs_rel_error']:.6f}"
        )

    print()
    print("Selected predicted periods")
    for vmode in range(16):
        vals = []
        for vincr in (1, 2, 4, 8, 16, 32, 64, 128, 160, 192, 224, 255):
            pred = candidate_period(vmode, vincr)
            vals.append("-" if pred is None else f"{pred:.1f}")
        print(f"  vmode {vmode:2d}: {vals}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Fit candidate formulas for vol_incr.summary.csv")
    ap.add_argument("summary", nargs="?", default="audio_tests/vol_incr.summary.csv")
    ap.add_argument("--json", default="audio_tests/vol_incr_fit.report.json")
    args = ap.parse_args()

    rows = load_rows(Path(args.summary))
    report = summarize_fit(rows)
    Path(args.json).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print_human(rows, report)
    print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
