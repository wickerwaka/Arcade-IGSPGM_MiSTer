"""T-VINCR: volume-envelope ramp rate discovery (VIncr 0x06 x VMode 0x12).

Measures the per-(VIncr,VMode) ramp rate by timing a full VolStart->VolEnd
ramp to its completion IRQ, in vblank units (real-time reference, no audio
capture).  Setup: an audible looping voice (so sample_tick advances the
envelope) with VolAcc=VolStart=0, VolEnd=max, VCtl ramp-up + vol-IRQ enable,
0x4A + 0x4D run bits set.  vblanks-to-vol-IRQ is inversely proportional to the
step rate, so the sweep reveals the rate law and (vs the sim baseline) any RTL
divergence.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_LOWER8, WIDTH_UPPER8, Voice

from .harness import Emit, quiesce

V = 4
MAX_VBLANKS = 600   # ~10 s cap; slower ramps recorded as incomplete
VINCR_SWEEP = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF]
VMODE_SWEEP = [0x00, 0x01, 0x02, 0x03]


def _measure(ics, vmode: int, vincr: int) -> dict:
    # Build the ramp voice: audible loop source, envelope window 0 -> max.
    v = Voice()
    v.osc_conf = 0x08          # loop, lin8 (keeps the oscillator running)
    v.osc_fc = 12081
    v.set_start_wave_addr(0x2D640)
    v.set_end_wave_addr(0x3B2A0)
    v.set_acc_wave_addr(0x2D640)
    v.pan = 0x80
    v.vol_acc = 0x0000         # start at floor
    v.vol_start = 0x00
    v.vol_end = 0xFF
    v.vol_incr = vincr
    v.vmode = vmode
    v.vol_ctrl = 0x20          # ramp up + vol-IRQ enable (bit5)
    v.osc_ctl = 0x00           # key on
    ics.reset_irq_counts()
    ics.write_voice(V, v)
    f0, _ = ics.get_irq_counts_timed(reset=True)
    done_vbl = -1
    while True:
        f1, c = ics.get_irq_counts_timed()
        if c.vol > 0:
            done_vbl = f1 - f0
            break
        if f1 - f0 >= MAX_VBLANKS:
            break
    # stop voice
    ics.write_reg(V, 0x10, 0x0F, width=WIDTH_UPPER8)
    ics.write_reg(V, 0x0D, 0x03, width=WIDTH_UPPER8)
    ics.write_reg(V, 0x00, 0x00, width=WIDTH_UPPER8)
    return {"ramp_vblanks": done_vbl, "completed": done_vbl >= 0}


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    # Voice-IRQ delivery needs 0x4A=1 (quiesce clears it) AND the 0x4D run
    # bits (quiesce sets 0x0D) — both required for the ramp-completion IRQ.
    ics.write_reg(0, 0x4A, 0x01, width=WIDTH_LOWER8)
    for vmode in VMODE_SWEEP:
        for vincr in VINCR_SWEEP:
            obs = _measure(ics, vmode, vincr)
            emit("t_vincr.ramp_rate", {"vmode": vmode, "vincr": vincr}, obs)
    quiesce(ics)
