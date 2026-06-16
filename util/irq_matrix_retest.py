#!/usr/bin/env python3
"""Hardware re-tests for register-matrix entries 00-C, P0-C, 10-D, P2-A.

Real ICS2115 chip via PicoROM debug link (ics_remote page). Globals are left at
the test-ROM BIOS init (writing 0x4D clobbers master-run). Run from the worktree:
  uv run python util/irq_matrix_retest.py
"""
from __future__ import annotations
import sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from util.ics2115_remote import ICS2115Remote, WIDTH_16, WIDTH_UPPER8, WIDTH_LOWER8

V = 5  # scratch voice


def main():
    r = ICS2115Remote.open("pgm", reset="low")
    for _ in range(60):
        try:
            r.ping(); break
        except Exception:
            time.sleep(0.25)
    print("link up; globals:",
          "0x4d=%#x" % r.read_global(0x4d, WIDTH_LOWER8),
          "0x4a=%#x" % r.read_global(0x4a, WIDTH_LOWER8),
          "0x0e=%#x" % r.read_global(0x0e, WIDTH_UPPER8))

    # ---- read-path sanity ----
    r.write_reg(0, 0x01, 0x1234, WIDTH_16)
    rb = r.read_reg(0, 0x01, WIDTH_16)
    print(f"\n[read-path] wrote osc_fc=0x1234 read=0x{rb:04x}  {'OK' if rb==0x1234 else 'FLAKY'}")

    def irqv():       # consuming read of IRQV (0x0F)
        return r.read_global(0x0f, WIDTH_UPPER8)
    def drain():
        for _ in range(40):
            if not (irqv() & 0x80): break

    # ---- 00-C: host osc pend needs bit7 AND bit5 ----
    print("\n[00-C] host osc pend: 0x80 (bit7 only) vs 0xA0 (bit7+bit5) on voice", V)
    for conf in (0x80, 0xA0):
        drain()
        r.write_reg(V, 0x10, 0x0F, WIDTH_UPPER8)      # ensure stopped
        r.write_reg(V, 0x00, conf, WIDTH_UPPER8)      # osc_conf write
        st = r.read_status_port()
        b4b = r.read_global(0x4b, WIDTH_16)
        iv = irqv()
        print(f"  osc_conf=0x{conf:02x}: status=0x{st:02x}(bit1={st>>1&1}) "
              f"0x4B=0x{b4b:04x}(pend_bit7={(b4b>>7)&1 or (b4b>>15)&1}) IRQV=0x{iv:02x}(valid={iv>>7&1},v={iv&0x1f})")
    drain()

    # ---- P0-C: status-port bit6 on real hardware ----
    print("\n[P0-C] status-port bit6 (RTL=write-FIFO-busy; real chip=?)")
    idle = [r.read_status_port() for _ in range(200)]
    b6_idle = sum((s >> 6) & 1 for s in idle)
    busy = []
    for i in range(200):
        r.write_reg(0, 0x01, i & 0xFFFF, WIDTH_16)    # generate host writes
        busy.append(r.read_status_port())
    b6_busy = sum((s >> 6) & 1 for s in busy)
    print(f"  idle: bit6 set {b6_idle}/200, values={sorted(set(idle))}")
    print(f"  during writes: bit6 set {b6_busy}/200, values={sorted(set(busy))}")

    # ---- 10-D: OscCtl (0x10) readback shape ----
    print("\n[10-D] osc_ctl readback (write -> read upper8)")
    for w in (0x00, 0x01, 0x02, 0x03, 0x0F, 0xAA, 0xFF):
        r.write_reg(V, 0x10, w, WIDTH_UPPER8)
        print(f"  wrote 0x{w:02x} -> read 0x{r.read_reg(V,0x10,WIDTH_UPPER8):02x}")

    # ---- P2-A: lone low / lone high write side-effects (osc_fc 0x01) ----
    print("\n[P2-A] lone-byte writes to osc_fc (0x01)")
    r.write_reg(V, 0x01, 0xAAAA, WIDTH_16); print(f"  16-bit 0xAAAA -> 0x{r.read_reg(V,0x01,WIDTH_16):04x}")
    r.write_reg(V, 0x01, 0x55, WIDTH_LOWER8); print(f"  then lone-LOW 0x55 -> 0x{r.read_reg(V,0x01,WIDTH_16):04x}")
    r.write_reg(V, 0x01, 0xAAAA, WIDTH_16)
    r.write_reg(V, 0x01, 0x33, WIDTH_UPPER8); print(f"  reset 0xAAAA then lone-HIGH 0x33 -> 0x{r.read_reg(V,0x01,WIDTH_16):04x}")
    r.close()


if __name__ == "__main__":
    main()
