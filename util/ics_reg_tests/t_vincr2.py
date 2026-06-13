"""T-VINCR2: dense volume-envelope step-rate characterization.

Measures the envelope step rate directly: key a voice at VolAcc=0 with a wide
window, then read VolAcc (0x09) at two vblank marks and take the delta over
the interval.  step_per_vblank = dVolAcc/dt — works in a couple seconds per
cell at any rate, and an adaptive interval avoids saturation (fast cells) and
under-resolution (slow cells).  Sweeps VIncr densely x VMode to derive the
hardware rate law for the calc_vol_step rework.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8, Voice

from .harness import Emit, quiesce

V = 4
VOL_MAX = 0xFFFF          # 16-bit readback of vol_acc bits 25:10
VINCR_SWEEP = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 255]
VMODE_SWEEP = [0, 1, 2, 3]
INTERVALS = [2, 4, 8, 16, 32, 64, 128]   # adaptive vblank windows


def _make(ics, vmode, vincr):
    v = Voice()
    v.osc_conf = 0x08
    v.osc_fc = 12081
    v.set_start_wave_addr(0x2D640)
    v.set_end_wave_addr(0x3B2A0)
    v.set_acc_wave_addr(0x2D640)
    v.pan = 0x80
    v.vol_acc = 0x0000
    v.vol_start = 0x00
    v.vol_end = 0xFF
    v.vol_incr = vincr
    v.vmode = vmode
    v.vol_ctrl = 0x00        # ramp up, no loop, no IRQ
    v.osc_ctl = 0x00
    return v


def _rate(ics, vmode, vincr) -> dict:
    # Adaptive: grow the interval until VolAcc moves measurably without
    # saturating; record the per-vblank step at the chosen interval.
    chosen = None
    for interval in INTERVALS:
        ics.write_voice(V, _make(ics, vmode, vincr))
        f0, _ = ics.get_irq_counts_timed()
        acc0 = ics.read_reg(V, 0x09, width=WIDTH_16)
        while True:
            f1, _ = ics.get_irq_counts_timed()
            if f1 - f0 >= interval:
                break
        acc1 = ics.read_reg(V, 0x09, width=WIDTH_16)
        ics.write_reg(V, 0x10, 0x0F, width=WIDTH_UPPER8)
        ics.write_reg(V, 0x00, 0x00, width=WIDTH_UPPER8)
        delta = acc1 - acc0
        saturated = acc1 >= VOL_MAX - 0x40
        chosen = {"interval": f1 - f0, "acc0": acc0, "acc1": acc1, "delta": delta,
                  "saturated": saturated,
                  "step_per_vblank": round(delta / max(f1 - f0, 1), 1)}
        # Shortest-first: stop once the step is well-resolved, or as soon as a
        # cell saturates (fast family — the shortest interval is the best we
        # can do and step_per_vblank is a lower bound).
        if saturated or delta >= 0x200:
            break
    return chosen


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    for vmode in VMODE_SWEEP:
        for vincr in VINCR_SWEEP:
            emit("t_vincr2.step_rate", {"vmode": vmode, "vincr": vincr},
                 _rate(ics, vmode, vincr))
    quiesce(ics)
