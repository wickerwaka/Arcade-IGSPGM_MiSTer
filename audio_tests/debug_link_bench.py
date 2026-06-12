#!/usr/bin/env python3
"""Benchmark the simulator debug-link transport (ICS remote protocol).

Measures emulated cycles and wall time per command for each remote operation,
plus a mixed reliability run. Run from repo root:

    python3 audio_tests/debug_link_bench.py [--ops N] [--reliability N]
"""

import argparse
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from util.ics2115_remote import ICS2115Remote, Voice  # noqa: E402


def ticks(remote) -> int:
    return int(remote.picorom.sim.call("sim.status")["total_ticks"])


def bench(remote, label, fn, count, cycles_per_frame, results):
    cyc = []
    wall = []
    for _ in range(count):
        t0 = ticks(remote)
        w0 = time.perf_counter()
        fn()
        wall.append(time.perf_counter() - w0)
        cyc.append(ticks(remote) - t0)
    row = {
        "label": label,
        "count": count,
        "cyc_med": statistics.median(cyc),
        "cyc_min": min(cyc),
        "cyc_max": max(cyc),
        "frames_med": statistics.median(cyc) / cycles_per_frame,
        "wall_med_ms": statistics.median(wall) * 1e3,
    }
    results.append(row)
    print(
        f"{label:18s} n={count:3d}  cycles med={row['cyc_med']:>9.0f} "
        f"min={row['cyc_min']:>9d} max={row['cyc_max']:>9d}  "
        f"frames med={row['frames_med']:.2f}  wall med={row['wall_med_ms']:.0f}ms",
        flush=True,
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ops", type=int, default=15, help="ops per benchmark")
    ap.add_argument("--reliability", type=int, default=100, help="mixed ops for reliability pass")
    ap.add_argument("--boot-frames", type=int, default=600, help="max frames to wait for first ping")
    args = ap.parse_args()

    t_start = time.perf_counter()
    remote = ICS2115Remote.open_sim(game="pgm_test", transport="debug_link")
    sim = remote.picorom.sim
    print(f"sim started in {time.perf_counter() - t_start:.1f}s", flush=True)

    # Boot: run frames until the first ping succeeds.
    boot_frames = 0
    boot_t0 = time.perf_counter()
    ping = None
    while boot_frames < args.boot_frames:
        sim.call("sim.run_frames", {"count": 10})
        boot_frames += 10
        try:
            ping = remote.ping()
            break
        except Exception:
            continue
    if ping is None:
        print("FAIL: ping never succeeded during boot window", flush=True)
        return 1
    print(
        f"first ping OK after {boot_frames} boot frames "
        f"({time.perf_counter() - boot_t0:.1f}s wall), driver magic 0x{ping.driver_magic:04x}",
        flush=True,
    )

    # Cycles per frame baseline.
    t0 = ticks(remote)
    sim.call("sim.run_frames", {"count": 5})
    cycles_per_frame = (ticks(remote) - t0) / 5
    print(f"cycles per frame: {cycles_per_frame:.0f}", flush=True)

    results = []
    voice = Voice.from_bios_trace()
    bench(remote, "ping", lambda: remote.ping(), args.ops, cycles_per_frame, results)
    bench(remote, "read_reg(pan)", lambda: remote.read_reg(0, "pan"), args.ops, cycles_per_frame, results)
    bench(remote, "write_reg(pan)", lambda: remote.write_reg(0, "pan", 0x80), args.ops, cycles_per_frame, results)
    bench(remote, "read_voice", lambda: remote.read_voice(0), args.ops, cycles_per_frame, results)
    bench(remote, "write_voice", lambda: remote.write_voice(0, voice), args.ops, cycles_per_frame, results)
    bench(remote, "get_irq_counts", lambda: remote.get_irq_counts(), args.ops, cycles_per_frame, results)

    # Reliability: mixed ops, verify readback values round-trip.
    errors = 0
    t0 = ticks(remote)
    w0 = time.perf_counter()
    for i in range(args.reliability):
        try:
            val = (i * 7) & 0xFF
            remote.write_reg(i % 32, "pan", val)
            got = remote.read_reg(i % 32, "pan")
            if got != val:
                errors += 1
                print(f"  MISMATCH op {i}: wrote {val:#x} read {got:#x}", flush=True)
        except Exception as exc:
            errors += 1
            print(f"  ERROR op {i}: {exc}", flush=True)
    rel_cyc = ticks(remote) - t0
    rel_wall = time.perf_counter() - w0
    n_ops = args.reliability * 2
    print(
        f"reliability: {n_ops} ops, {errors} errors, "
        f"{rel_cyc / n_ops:.0f} cycles/op ({rel_cyc / n_ops / cycles_per_frame:.2f} frames/op), "
        f"{rel_wall / n_ops * 1e3:.0f}ms/op wall, total {rel_wall:.1f}s",
        flush=True,
    )

    remote.close()
    print("done", flush=True)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
