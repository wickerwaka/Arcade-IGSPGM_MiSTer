#!/usr/bin/env python3
"""Settle matrix 00-C: when does a host osc-IRQ pend latch on real silicon?

Part A validates detection with a REAL osc IRQ (a one-shot voice that runs to
its end with IRQ enabled).  Part B tests the host-set pend (writing osc_conf
bit7 / bit7+bit5) under several voice states.  Detection uses both the Z80 IRQ
counters (asserted IRQs) and IRQV/0x4B reads (latched pends).
"""
from __future__ import annotations
import sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from util.ics2115_remote import ICS2115Remote, Voice, WIDTH_16, WIDTH_UPPER8, WIDTH_LOWER8

V = 5
REGION = 0x100000


def boot(r):
    for _ in range(60):
        try:
            r.ping(); return
        except Exception:
            time.sleep(0.25)
    raise SystemExit("no link")


def snapshot(r, label):
    c = r.get_irq_counts()
    b4b = r.read_global(0x4b, WIDTH_16)
    iv = r.read_global(0x0f, WIDTH_UPPER8)   # consuming
    print(f"  {label}: osc_irq={c.osc} vol_irq={c.vol} 0x4B=0x{b4b:04x} IRQV=0x{iv:02x}")


def oneshot_voice():
    v = Voice(osc_conf=0x20, osc_fc=0x0800, vol_acc=0xFFFF, vol_start=0, vol_end=0xFF,
              vol_incr=0xFF, vmode=2, vol_ctrl=0, pan=0x80, osc_ctl=0x0F, osc_saddr=0x40)
    v.set_start_wave_addr(REGION); v.set_end_wave_addr(REGION + 0x100); v.set_acc_wave_addr(REGION)
    return v


def main():
    r = ICS2115Remote.open("pgm", reset="low")
    boot(r)
    print("globals 0x4d=%#x 0x4a=%#x 0x0e=%#x" % (
        r.read_global(0x4d, WIDTH_LOWER8), r.read_global(0x4a, WIDTH_LOWER8), r.read_global(0x0e, WIDTH_UPPER8)))

    # ---- Part A: real one-shot osc IRQ (validates detection) ----
    print("\n[A] real one-shot osc IRQ on voice", V)
    r.reset_irq_counts()
    r.write_reg(V, 0x10, 0x0F, WIDTH_UPPER8)
    r.write_voice(V, oneshot_voice())
    r.write_reg(V, 0x10, 0x00, WIDTH_UPPER8)   # key on -> runs to end -> IRQ
    time.sleep(0.1)
    snapshot(r, "after one-shot end")

    # ---- Part B: host-set pend, several voice states ----
    print("\n[B] host-set pend (osc_conf bit7 / bit7+bit5) under different states")
    for state, prep in [
        ("stopped (ctl=0x0F)", lambda: r.write_reg(V, 0x10, 0x0F, WIDTH_UPPER8)),
        ("keyed-on (ctl=0x00)", lambda: (r.write_voice(V, oneshot_voice()), r.write_reg(V, 0x10, 0x00, WIDTH_UPPER8))),
    ]:
        for conf in (0x80, 0xA0):
            r.reset_irq_counts()
            for _ in range(40):
                if not (r.read_global(0x0f, WIDTH_UPPER8) & 0x80): break
            prep()
            r.write_reg(V, 0x00, conf, WIDTH_UPPER8)
            cb = r.read_reg(V, 0x00, WIDTH_UPPER8)
            time.sleep(0.02)
            print(f"  state={state} conf=0x{conf:02x} osc_conf_rb=0x{cb:02x}", end="")
            snapshot(r, "")
    r.close()


if __name__ == "__main__":
    main()
