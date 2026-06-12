"""T-PROBE: reserved-register READ probing (safe on hardware).

Matrix rows: 13-A, 15-A, 44-A, 48-A, 4E-A (read side).

Write probing lives in t_probe_w — on real hardware, writes to reserved
voice registers are dangerous: 0xFFFF to 0x13 can hang the ICS host
interface (BUSY stuck) and degrade voice-register access for the rest of
the session (observed 2026-06-11).  Run t_probe_w last, or not at all.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8

from .harness import Emit, quiesce

PROBE_VOICE = 0

VOICE_RANGE = list(range(0x13, 0x20))     # voice-selected decode in RTL
MID_RANGE = list(range(0x20, 0x40))       # global-side decode in RTL
GLOBAL_UNUSED = [0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4E]

# NOTE: matrix row 4F-C (osc_select values > 0x1F) cannot be expressed through
# the current Z80 driver: every voice-register access re-issues a masked voice
# select.  Needs a raw-select driver command; deferred.


def _probe_reads(ics, emit: Emit, reg: int, test_id: str) -> None:
    emit(test_id, {"reg": reg}, {"reads": {
        "w16": ics.read_reg(PROBE_VOICE, reg, width=WIDTH_16),
        "hi8": ics.read_reg(PROBE_VOICE, reg, width=WIDTH_UPPER8),
        "lo8": ics.read_reg(PROBE_VOICE, reg, width=WIDTH_LOWER8),
    }})


def _hi_select_read(ics, emit: Emit) -> None:
    # Read-only sweep of register-select values above 0x4F: mirrors/aliases
    # show up as non-default values matching real registers.
    reads = [ics.read_reg(PROBE_VOICE, reg, width=WIDTH_16) for reg in range(0x50, 0x100)]
    emit("t_probe.hi_select_read", {"first_reg": 0x50}, {"reads": reads})


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    for reg in VOICE_RANGE:
        _probe_reads(ics, emit, reg, "t_probe.voice_reserved")
    for reg in MID_RANGE:
        _probe_reads(ics, emit, reg, "t_probe.mid_reserved")
    for reg in GLOBAL_UNUSED:
        _probe_reads(ics, emit, reg, "t_probe.global_unused")
    _hi_select_read(ics, emit)
