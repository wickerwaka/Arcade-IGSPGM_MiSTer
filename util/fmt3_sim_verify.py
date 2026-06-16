#!/usr/bin/env python3
"""Verify in the simulator that fmt=3 now decodes as u-law (== fmt=1).

Captures sample-exact sim audio for voice 0 in fmt 0/1/3 on the same ROM loop
and checks fmt3 == fmt1 (the fix) and fmt3 != fmt0.
"""
from __future__ import annotations
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from util.ics2115_remote import ICS2115Remote, Voice

REGION = 0x2D640   # BIOS music-ROM sample area (non-silent)
SPAN = 0x0200
FC = 0x0400
N = 4000


def make_voice(conf):
    v = Voice(osc_conf=conf, osc_fc=FC, vol_acc=0xFFFF, vol_start=0, vol_end=0xFF,
              vol_incr=0xFF, vmode=2, vol_ctrl=0, pan=0x80, osc_ctl=0x0F, osc_saddr=0x40)
    v.set_start_wave_addr(REGION); v.set_end_wave_addr(REGION + SPAN); v.set_acc_wave_addr(REGION)
    return v


def cap(remote, conf):
    remote.write_reg(0, 0x10, 0x0F)
    remote.write_voice(0, make_voice(conf))
    remote.write_reg(0, 0x10, 0x00)
    return np.array(remote.capture_audio_frames(N, timeout=None), dtype=np.int64)


def main():
    remote = ICS2115Remote.open_sim(
        game="pgm_test", transport="debug_link",
        read_timeout_cycles=20_000_000, timeout_cycles_per_byte=20_000_000)
    remote.picorom.sim.call("sim.run_frames", {"count": 200})
    remote.ping()
    remote.write_global(0x4d, 0x05); remote.write_global(0x4a, 0x01); remote.write_global(0x0e, 0x1f)
    remote.open_audio()
    f0 = cap(remote, 0x08); f1 = cap(remote, 0x09); f3 = cap(remote, 0x0b)
    remote.close()
    L = min(len(f0), len(f1), len(f3)); s = 600
    f0 = f0[s:L, 0].astype(float); f1 = f1[s:L, 0].astype(float); f3 = f3[s:L, 0].astype(float)
    def rms(a): return float(np.sqrt((a ** 2).mean()))
    def best_xcorr(a, b, maxlag):
        a = (a - a.mean()); b = (b - b.mean()); n = len(a) - maxlag; best = -2.0
        an = a[:n]
        for lag in range(maxlag):
            bb = b[lag:lag + n]
            c = float(np.dot(an, bb) / (np.linalg.norm(an) * np.linalg.norm(bb) + 1e-9))
            if c > best: best = c
        return best
    print(f"rms  fmt0={rms(f0):.0f}  fmt1={rms(f1):.0f}  fmt3={rms(f3):.0f}")
    ml = 700  # > one loop period
    print(f"fmt3 vs fmt1 (u-law) best xcorr = {best_xcorr(f3, f1, ml):.4f}  (1.0 => identical decode)")
    print(f"fmt3 vs fmt0 (8-bit) best xcorr = {best_xcorr(f3, f0, ml):.4f}")


if __name__ == "__main__":
    main()
