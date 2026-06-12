#!/usr/bin/env python3
"""Run ICS2115 register conformance tests on the simulator or real hardware.

    python3 util/run_ics_reg_tests.py --target sim --out results/sim.jsonl
    python3 util/run_ics_reg_tests.py --target hw  --out results/hw.jsonl

Both targets drive the identical TestROM/Z80 register path; records contain
only register values, so two runs are directly diffable with
util/compare_ics_results.py.  See docs/ics2115_register_matrix.md.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from util.ics2115_remote import ICS2115Remote  # noqa: E402
from util.ics_reg_tests import GROUPS  # noqa: E402


def open_target(args):
    if args.target == "sim":
        remote = ICS2115Remote.open_sim(game=args.game, transport="debug_link")
        sim = remote.picorom.sim
        # Boot until the TestROM answers.
        for _ in range(60):
            sim.call("sim.run_frames", {"count": 10})
            try:
                remote.ping()
                return remote
            except Exception:
                continue
        remote.close()
        raise RuntimeError("TestROM never answered ping during boot window")
    remote = ICS2115Remote.open(args.hw_target, reset=args.hw_reset or None)
    # The board needs a moment to boot the TestROM after a reset release.
    deadline = time.time() + 10.0
    while True:
        try:
            remote.ping()
            return remote
        except Exception:
            if time.time() >= deadline:
                raise
            time.sleep(0.5)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", required=True, choices=["sim", "hw"])
    ap.add_argument("--game", default="pgm_test", help="sim game target")
    ap.add_argument("--hw-target", default="pgm", help="pypicorom target name")
    ap.add_argument("--hw-reset", default="low",
                    help="reset value asserted before connecting (empty to skip)")
    ap.add_argument("--groups", nargs="*", default=list(GROUPS), choices=list(GROUPS))
    ap.add_argument("--exclude", nargs="*", default=[],
                    help="skip records whose test_id contains any of these substrings")
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    out = args.out.open("w")
    out.write(json.dumps({"_meta": {
        "target": args.target,
        "groups": args.groups,
        "exclude": args.exclude,
    }}) + "\n")

    counts: dict[str, int] = {}

    def emit(test_id: str, params: dict, obs: dict) -> None:
        if any(pattern in test_id for pattern in args.exclude):
            return
        out.write(json.dumps({"test_id": test_id, "params": params, "obs": obs},
                             sort_keys=True) + "\n")
        out.flush()
        counts[test_id] = counts.get(test_id, 0) + 1

    remote = open_target(args)
    started = time.perf_counter()
    try:
        for group in args.groups:
            print(f"[{args.target}] running {group}...", file=sys.stderr, flush=True)
            GROUPS[group](remote, emit)
    finally:
        remote.close()
        out.close()

    total = sum(counts.values())
    for test_id in sorted(counts):
        print(f"  {test_id}: {counts[test_id]} records", file=sys.stderr)
    print(f"[{args.target}] {total} records -> {args.out} "
          f"({time.perf_counter() - started:.0f}s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
