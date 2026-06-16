#!/usr/bin/env python3
"""Configure ICS2115 voice 0 on hardware and key it on, then exit.

The voice keeps playing autonomously (looping) after we disconnect, so the audio
can be recorded externally (Elgato / USB Sound Device input).  Used to probe
voice format behavior on real hardware.

Usage: ics_voice_setup.py <osc_conf_hex> <osc_fc_hex> <region_hex> [--off]
  e.g. ics_voice_setup.py 0x08 0x0400 0x100000   # fmt0 8-bit linear, loop
       ics_voice_setup.py 0x0b 0x0400 0x100000   # fmt3
       ics_voice_setup.py --off                  # silence voice 0
"""
from __future__ import annotations
import sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from util.ics2115_remote import ICS2115Remote, Voice

SPAN = 0x8000


def boot(remote):
    for _ in range(40):
        try:
            remote.ping(); return
        except Exception:
            time.sleep(0.25)
    raise SystemExit("link did not come up")


def main():
    remote = ICS2115Remote.open("pgm", reset="low")
    boot(remote)
    if "--off" in sys.argv:
        remote.write_reg(0, 0x10, 0x0F)
        remote.close(); print("voice 0 off"); return
    conf = int(sys.argv[1], 16); fc = int(sys.argv[2], 16); region = int(sys.argv[3], 16)
    remote.write_global(0x4d, 0x05)
    remote.write_global(0x4a, 0x01)
    remote.write_global(0x0e, 0x1f)
    v = Voice(osc_conf=conf, osc_fc=fc, vol_acc=0xFFFF, vol_start=0x00, vol_end=0xFF,
              vol_incr=0xFF, vmode=0x02, vol_ctrl=0x00, pan=0x80, osc_ctl=0x0F, osc_saddr=0x40)
    v.set_start_wave_addr(region); v.set_end_wave_addr(region + SPAN); v.set_acc_wave_addr(region)
    remote.write_reg(0, 0x10, 0x0F)
    remote.write_voice(0, v)
    remote.write_reg(0, 0x10, 0x00)   # key on
    remote.close()
    print(f"voice0 conf=0x{conf:02x} fc=0x{fc:04x} region=0x{region:06x} keyed on")


if __name__ == "__main__":
    main()
