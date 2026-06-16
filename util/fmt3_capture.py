#!/usr/bin/env python3
"""Persistent-connection hardware capture of ICS2115 voice formats.

One debug-link connection (single reset); globals left at the test-ROM BIOS init
(writing 0x4D clobbers master-run -> silence).  Reconfigures voice 0 live between
recordings of the analog feed (USB Sound Device).  Saves /tmp/cap_<name>.wav.
"""
from __future__ import annotations
import sys, time, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from util.ics2115_remote import ICS2115Remote, Voice

AUDIO_DEV = ":3"
SECS = 3
A, B = 0x100000, 0x300000

# name, osc_conf, osc_fc, region, span
CONFIGS = [
    ("fmt0_A", 0x08, 0x0400, A, 0x8000),     # 8-bit linear (reference)
    ("fmt1_A", 0x09, 0x0400, A, 0x8000),     # 8-bit u-law
    ("fmt2_A", 0x0a, 0x0400, A, 0x8000),     # 16-bit
    ("fmt3_A", 0x0b, 0x0400, A, 0x8000),     # the format in question
    ("fmt3_B", 0x0b, 0x0400, B, 0x8000),     # different ROM region
    ("fmt3_short", 0x0b, 0x0400, A, 0x0200), # short loop -> periodic if it reads ROM
    ("fmt0_short", 0x08, 0x0400, A, 0x0200), # short-loop reference
    ("fmt3_lowfc", 0x0b, 0x0100, A, 0x8000),
    ("fmt3_hifc", 0x0b, 0x1000, A, 0x8000),
]


def main():
    r = ICS2115Remote.open("pgm", reset="low")
    for _ in range(60):
        try:
            info = r.ping(); print(f"link up, magic=0x{info.driver_magic:04x}"); break
        except Exception:
            time.sleep(0.25)
    else:
        raise SystemExit("no link")
    for name, conf, fc, region, span in CONFIGS:
        v = Voice(osc_conf=conf, osc_fc=fc, vol_acc=0xFFFF, vol_start=0, vol_end=0xFF,
                  vol_incr=0xFF, vmode=2, vol_ctrl=0, pan=0x80, osc_ctl=0x0F, osc_saddr=0x40)
        v.set_start_wave_addr(region); v.set_end_wave_addr(region + span); v.set_acc_wave_addr(region)
        r.write_reg(0, 0x10, 0x0F); r.write_voice(0, v); r.write_reg(0, 0x10, 0x00)
        time.sleep(0.3)
        subprocess.run(["ffmpeg", "-y", "-f", "avfoundation", "-i", AUDIO_DEV,
                        "-t", str(SECS), "-ac", "2", f"/tmp/cap_{name}.wav"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"captured {name}")
    r.write_reg(0, 0x10, 0x0F)
    r.close()


if __name__ == "__main__":
    main()
