#!/usr/bin/env python3
"""Characterize ICS2115 voice format 3 (osc_conf format bits = 0b11) on hardware.

The RTL currently assumes fmt=3 is an oscillator-clocked LFSR noise generator
(ROM ignored).  This probes real hardware to find out what it actually does:
captures fmt=3 at two different sample-ROM regions and fmt=0 (8-bit linear) at
one region, all at the same osc_fc, so we can tell whether the output depends on
ROM content (=> it reads ROM) and how it relates to the 8-bit-linear decode.

Saves raw L/R samples to /tmp/fmt3_probe.npz for offline analysis.
"""
from __future__ import annotations
import sys, time
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from util.ics2115_remote import ICS2115Remote, Voice

# osc_conf format bits [1:0] = {OSC_16BIT(1), OSC_ULAW(0)}; bit3 = OSC_LOOP
CONF_LIN8  = 0x08   # 00 + loop : 8-bit linear
CONF_FMT3  = 0x0B   # 11 + loop : the "noise?" format
OSC_FC     = 0x0400 # ~1 ROM byte advance per output sample
REGION_A   = 0x100000
REGION_B   = 0x300000
SPAN       = 0x8000
NBLOCKS    = 300    # ~1.16 s at 33 kHz, 128 samples/block


def make_voice(conf: int, start: int, fc: int) -> Voice:
    v = Voice(osc_conf=conf, osc_fc=fc, vol_acc=0xFFFF, vol_start=0x00,
              vol_end=0xFF, vol_incr=0xFF, vmode=0x02, vol_ctrl=0x00,
              pan=0x80, osc_ctl=0x0F, osc_saddr=0x40)
    v.set_start_wave_addr(start)
    v.set_end_wave_addr(start + SPAN)
    v.set_acc_wave_addr(start)
    return v


def capture(remote, conf, start, fc, nblocks=NBLOCKS):
    remote.write_reg(0, 0x10, 0x0F)            # stop voice 0
    remote.write_voice(0, make_voice(conf, start, fc))
    remote.write_reg(0, 0x10, 0x00)            # key on
    audio = remote.open_audio()
    samples = []
    for block in audio.capture_blocks(nblocks, timeout=nblocks * 0.05 + 10):
        samples.extend(block.samples)
    remote.write_reg(0, 0x10, 0x0F)            # stop
    return np.array(samples, dtype=np.int32)


def main():
    remote = ICS2115Remote.open("pgm", reset="low")
    # wait for the ics_remote page to boot after the reset pulse
    for _ in range(40):
        try:
            info = remote.ping(); print(f"driver_magic=0x{info.driver_magic:04x}"); break
        except Exception:
            time.sleep(0.25)
    else:
        raise SystemExit("link did not come up")
    # globals: master run (0x4D bits0&2), voice-IRQ gate, 32 active osc (~33 kHz)
    remote.write_global(0x4d, 0x05)
    remote.write_global(0x4a, 0x01)
    remote.write_global(0x0e, 0x1f)

    out = {}
    out["fmt3_A"] = capture(remote, CONF_FMT3, REGION_A, OSC_FC)
    print("fmt3_A", out["fmt3_A"].shape)
    out["fmt3_B"] = capture(remote, CONF_FMT3, REGION_B, OSC_FC)
    print("fmt3_B", out["fmt3_B"].shape)
    out["lin8_A"] = capture(remote, CONF_LIN8, REGION_A, OSC_FC)
    print("lin8_A", out["lin8_A"].shape)
    remote.close()
    np.savez("/tmp/fmt3_probe.npz", **{k: v for k, v in out.items()})
    print("saved /tmp/fmt3_probe.npz")


if __name__ == "__main__":
    main()
