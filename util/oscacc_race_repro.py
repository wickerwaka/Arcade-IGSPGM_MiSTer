#!/usr/bin/env python3
"""Reproduce the OscAcc clobber race in the ICS2115 RTL.

The 32 voices share one wide dual-port RAM word per voice.  Both the host
register-write path (port B) and the sample sequencer (port A) do a whole-voice
read-modify-write of that word.  The only interlock blocks the host from
*starting* a write while the sequencer is on that voice; it does not stop the
sequencer from loading the voice during the host's 3-cycle commit.  When that
happens the sequencer captures the pre-write OscAcc and writes the whole voice
back afterwards, dropping the host's OscAcc write — exactly the "OscAcc outside
[OscStart,OscEnd] -> random sound" symptom.

This stresses register 0x0a (OscAcc high) on the Z80 (thousands of real host-bus
writes per round-trip), reading back each write and counting mismatches.

  POSITIVE: master-run on, stress a voice the sequencer processes -> mismatches.
  CONTROL : stress a voice ABOVE active_osc (never processed)     -> ~zero.

Usage:
  python3 util/oscacc_race_repro.py --target sim   [--iters 1000 --calls 40]
  python3 util/oscacc_race_repro.py --target hw
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from util.ics2115_remote import ICS2115Remote


def run_phase(remote, name, voice, reg, iters, calls):
    total = 0
    mism = 0
    for _ in range(calls):
        mism += remote.stress_reg(voice, reg, iters)
        total += iters
    pct = 100.0 * mism / total if total else 0.0
    print(f"{name:<10} voice={voice:2d} reg=0x{reg:02x}: {mism}/{total} clobbered ({pct:.3f}%)")
    return mism, total


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", choices=["sim", "hw"], required=True)
    ap.add_argument("--reg", type=lambda x: int(x, 0), default=0x0A, help="register to stress (default OscAcc high 0x0a)")
    ap.add_argument("--iters", type=int, default=1000, help="iterations per round-trip")
    ap.add_argument("--calls", type=int, default=40, help="round-trips per phase")
    ap.add_argument("--control-voice", type=int, default=20)
    ap.add_argument("--game", default="pgm_test")
    ap.add_argument("--prime-frames", type=int, default=200)
    args = ap.parse_args()

    if args.target == "sim":
        remote = ICS2115Remote.open_sim(
            game=args.game, transport="debug_link",
            read_timeout_cycles=400_000_000, timeout_cycles_per_byte=400_000_000,
        )
        remote.picorom.sim.call("sim.run_frames", {"count": args.prime_frames})
    else:
        remote = ICS2115Remote.open("pgm", reset="low")

    try:
        info = remote.ping()
        print(f"driver_magic=0x{info.driver_magic:04x}\n")

        # CONTROL: only voices 0..1 are processed, so the control voice (20) is
        # never touched by the sequencer -> its host writes must always stick.
        remote.write_global("active_osc", 1)
        run_phase(remote, "control", args.control_voice, args.reg, args.iters, args.calls)

        # POSITIVE: all 32 voices processed every sample -> sequencer is always
        # busy and races the host write to voice 0.
        remote.write_global("active_osc", 31)
        run_phase(remote, "positive", 0, args.reg, args.iters, args.calls)
    finally:
        remote.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
