"""T-IRQV: voice IRQ vector semantics via host-forced pending bits.

Matrix rows: 0F-A, 0F-B, 0F-C, 0F-D, 00-C, 00-D, 4B-A, 4B-B.

Key trick (hardware-corrected 2026-06-11): a host pend requires BOTH bit7
and bit5 — writing 0x80 alone latches nothing on real hardware (recorded as
t_irqv.pend_nolatch).  Writing 0xA0 pends visibly in IRQV/0x4B but does NOT
assert the IRQ output (host pends never interrupt the Z80), so the pend can
be inspected and hand-drained step by step on both targets.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_LOWER8, WIDTH_UPPER8

from .harness import NUM_VOICES, Emit, quiesce


def _pend_silent(ics, emit: Emit) -> None:
    # bit7 without bit5: hardware latches nothing (sim currently does).
    ics.write_reg(7, 0x00, 0x80, width=WIDTH_UPPER8)
    emit("t_irqv.pend_nolatch", {"voice": 7},
         {"conf_readback": ics.read_reg(7, 0x00, width=WIDTH_UPPER8),
          "irqv": ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)})
    ics.write_reg(7, 0x00, 0x00, width=WIDTH_UPPER8)

    # Single voice, pend via bit7|bit5 (host pends do not assert the line).
    ics.reset_irq_counts()
    ics.write_reg(7, 0x00, 0xA0, width=WIDTH_UPPER8)
    conf = ics.read_reg(7, 0x00, width=WIDTH_UPPER8)
    vec1 = ics.read_reg(0, 0x4B, width=WIDTH_LOWER8)
    vec2 = ics.read_reg(0, 0x4B, width=WIDTH_LOWER8)
    irqv1 = ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)  # may auto-clear
    irqv2 = ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)
    conf_after = ics.read_reg(7, 0x00, width=WIDTH_UPPER8)
    counts = ics.reset_irq_counts()
    emit(
        "t_irqv.pend_silent",
        {"voice": 7},
        {
            "conf_readback": conf,
            "vec_4b": [vec1, vec2],
            "irqv_first": irqv1,
            "irqv_second": irqv2,
            "conf_after_irqv_reads": conf_after,
            "z80_osc_irqs": counts.osc,  # expect 0: no enable, no IRQ line
        },
    )
    ics.write_reg(7, 0x00, 0x00, width=WIDTH_UPPER8)


def _seed_pointer(ics, voice: int = 31) -> None:
    """Precondition the IRQV round-robin pointer deterministically: pend one
    sentinel voice, consume it so last_reported = voice.  Seeding 31 makes
    any round-robin-from-last scan wrap to start at voice 0 — equivalent to a
    from-zero scan, so the test result is independent of the hardware's scan
    style and of whatever ran before."""
    ics.write_reg(voice, 0x00, 0xA0, width=WIDTH_UPPER8)
    for _ in range(NUM_VOICES + 1):
        if (ics.read_reg(0, 0x0F, width=WIDTH_UPPER8) & 0xC0) == 0xC0:
            break
    ics.write_reg(voice, 0x00, 0x00, width=WIDTH_UPPER8)


def _multi_pend_order(ics, emit: Emit) -> None:
    # Pend three voices, then drain IRQV by hand: scan order and auto-clear
    # granularity.  Seed the pointer so the scan starts at voice 0.
    _seed_pointer(ics)
    for voice in (9, 3, 17):
        ics.write_reg(voice, 0x00, 0xA0, width=WIDTH_UPPER8)
    drain = []
    vecs = []
    for _ in range(5):
        vecs.append(ics.read_reg(0, 0x4B, width=WIDTH_LOWER8))
        drain.append(ics.read_reg(0, 0x0F, width=WIDTH_UPPER8))
        if (drain[-1] & 0xC0) == 0xC0:
            break
    emit(
        "t_irqv.multi_pend_order",
        {"voices_pend_order": [9, 3, 17]},
        {"irqv_sequence": drain, "vec_4b_sequence": vecs},
    )
    for voice in (9, 3, 17):
        ics.write_reg(voice, 0x00, 0x00, width=WIDTH_UPPER8)


def _active_osc_gating(ics, emit: Emit) -> None:
    # Pend a voice above ActiveOsc: is it hidden from the IRQV scan, and does
    # it survive until ActiveOsc re-includes it? (0F-D / 0E-B)
    _seed_pointer(ics)
    ics.write_reg(0, 0x0E, 0x0A, width=WIDTH_UPPER8)   # voices 0..10 active
    ics.write_reg(17, 0x00, 0xA0, width=WIDTH_UPPER8)
    irqv_gated = ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)
    ics.write_reg(0, 0x0E, 0x1F, width=WIDTH_UPPER8)   # all voices active
    irqv_ungated = ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)
    irqv_after = ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)
    emit(
        "t_irqv.active_osc_gating",
        {"voice": 17, "active_osc_gate": 0x0A},
        {"irqv_while_gated": irqv_gated, "irqv_after_ungate": irqv_ungated,
         "irqv_after_drain": irqv_after},
    )
    ics.write_reg(17, 0x00, 0x00, width=WIDTH_UPPER8)


def _pend_enabled(ics, emit: Emit) -> None:
    # Live delivery: pending + enable -> Z80 handler drains; the I-2 log
    # captures the raw IRQV byte it saw (vector encoding check).
    ics.reset_irq_counts()
    ics.clear_irq_log()
    ics.write_reg(5, 0x00, 0xA0, width=WIDTH_UPPER8)
    # Give the handler a frame to run (any command costs one frame).
    f0, _ = ics.get_irq_counts_timed()
    f1, counts = ics.get_irq_counts_timed()
    log = ics.get_irq_log()
    emit(
        "t_irqv.pend_enabled",
        {"voice": 5},
        {
            "z80_osc_irqs": counts.osc,
            "z80_vol_irqs": counts.vol,
            "z80_spurious": counts.spurious,
            "log": [[e.kind, e.a, e.b] for e in log],
            "conf_after": ics.read_reg(5, 0x00, width=WIDTH_UPPER8),
            "irqv_after": ics.read_reg(0, 0x0F, width=WIDTH_UPPER8),
        },
    )
    ics.write_reg(5, 0x00, 0x00, width=WIDTH_UPPER8)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    _pend_silent(ics, emit)
    _multi_pend_order(ics, emit)
    _active_osc_gating(ics, emit)
    _pend_enabled(ics, emit)
    quiesce(ics)
