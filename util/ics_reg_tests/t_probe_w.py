"""T-PROBE-W: reserved-register WRITE probing with alias detection.

DANGEROUS ON HARDWARE: writes to reserved voice registers are
state-dependent hazards — 0xFFFF to 0x13 can hang the ICS host interface
(BUSY stuck, Z80 command timeout) and degrade voice-register access for the
rest of the session.  Run this group LAST and expect to power-cycle/redeploy
afterwards.  Known hardware side effect: writing 0x20 flips the 0x4A/0x4B
readbacks from 0x02 to 0x80 (IRQ/vector block coupling).
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16

from .harness import (
    Emit,
    dict_delta,
    quiesce,
    snapshot_globals,
    snapshot_voice,
)
from .t_probe import GLOBAL_UNUSED, MID_RANGE, PROBE_VOICE, VOICE_RANGE


class Baseline:
    def __init__(self, ics):
        self.take(ics)

    def take(self, ics) -> None:
        self.voice = snapshot_voice(ics, PROBE_VOICE)
        self.globals = snapshot_globals(ics)


def _probe_writes(ics, emit: Emit, reg: int, test_id: str, baseline: Baseline) -> None:
    writes = {}
    for value in (0xFFFF, 0xA5C3):
        ics.write_reg(PROBE_VOICE, reg, value, width=WIDTH_16)
        writes[f"{value:#06x}"] = ics.read_reg(PROBE_VOICE, reg, width=WIDTH_16)

    voice_after = snapshot_voice(ics, PROBE_VOICE)
    globals_after = snapshot_globals(ics)
    counts = ics.reset_irq_counts()
    irq_delta = counts.timer0 + counts.timer1 + counts.osc + counts.vol + counts.spurious

    voice_aliased = baseline.voice != voice_after
    global_alias = dict_delta(baseline.globals, globals_after)
    emit(
        test_id,
        {"reg": reg},
        {
            "write_readback": writes,
            "voice_aliased": [baseline.voice, voice_after] if voice_aliased else None,
            "global_aliased": global_alias or None,
            "irq_count_delta": irq_delta,
        },
    )
    if voice_aliased or global_alias or irq_delta:
        quiesce(ics)
        baseline.take(ics)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    baseline = Baseline(ics)
    for reg in VOICE_RANGE:
        _probe_writes(ics, emit, reg, "t_probe_w.voice_reserved", baseline)
    for reg in MID_RANGE:
        _probe_writes(ics, emit, reg, "t_probe_w.mid_reserved", baseline)
    for reg in GLOBAL_UNUSED:
        _probe_writes(ics, emit, reg, "t_probe_w.global_unused", baseline)
