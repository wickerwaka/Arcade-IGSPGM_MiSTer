#!/usr/bin/env python3
"""Verify the MDFourier tone-sweep envelope in the sim: each tone should ramp up,
hold, ramp DOWN (key-off), then a 1-frame silent gap before the next tone."""
from __future__ import annotations
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from util.ics2115_remote import ICS2115Remote
from util.mdfourier import mdf_signal as sig

N = 95000  # ~172 frames: sync(20)+silence(48)+~4 tones


def main():
    r = ICS2115Remote.open_sim(game="pgm_test", transport="debug_link",
                               read_timeout_cycles=20_000_000, timeout_cycles_per_byte=20_000_000)
    r.picorom.sim.call("sim.run_frames", {"count": 200})
    r.ping()
    from util.mdfourier.run import make_voice
    r.write_voice(0, make_voice()); r.write_voice(1, make_voice())
    r.open_audio()
    r.mdf_load(sig.pack_script(sig.build_script()))
    r.mdf_start(len(sig.build_script()), sig.TIMER_SCALE0, sig.TIMER_PRESET0)
    s = np.array(r.capture_audio_frames(N, timeout=None), dtype=np.float64)
    r.close()
    x = np.abs(s.mean(axis=1))
    spf = sig.SAMPLES_PER_FRAME
    tones_start = int((sig.SYNC_PULSE_COUNT * 2 * sig.SYNC_PULSE_FRAMES + sig.SILENCE_FRAMES) * spf)
    print(f"captured {len(s)} samples; tones start ~frame {tones_start/spf:.0f} (sample {tones_start})")
    # envelope of the first ~3 tone elements (each TONE_FRAMES+gap), in frame bins
    elem = (sig.TONE_FRAMES + sig.TONE_GAP_FRAMES)
    for t in range(3):
        base = tones_start + int(t * elem * spf)
        print(f"\ntone {t}: per-frame RMS (frames 0..{elem-1}; last is the gap)")
        for fr in range(elem):
            a = base + int(fr * spf); b = base + int((fr + 1) * spf)
            seg = x[a:b]
            if len(seg) < 10: break
            rms = np.sqrt((seg ** 2).mean())
            bar = "#" * int(rms / 200)
            tag = " <-ramp-up" if fr == 0 else (" <-GAP/ramp-down" if fr >= sig.TONE_FRAMES - 0 else "")
            print(f"  f{fr:2d} rms={rms:6.0f} {bar}{tag}")


if __name__ == "__main__":
    main()
