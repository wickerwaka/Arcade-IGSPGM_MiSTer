"""T-IRQ: timer IRQ enable/pending/acknowledge semantics.

Matrix rows: 40-B, 43-A, 43-C, 4A-A, 4A-C, P2-B (timer half).

Strategy: run timer 0 at ~103 Hz with the Z80-facing enable bit CLEAR, so
pending state can be inspected at leisure (no handler ack racing us), then
flip the enable to observe latched-pending delivery.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8

from .harness import Emit, quiesce

TIMER_SCALE = 0xFF  # mult 32, shift 7
TIMER_PRESET = 4    # 327680 clocks ~ 103 Hz

# Slow configuration for ack/sticky tests: the re-pend window must be much
# longer than the one-frame command gap between an ack and its check read,
# or the timer re-pends before we can observe the cleared state.
SLOW_PRESET = 60    # 32*61*2048 = 3997696 clocks ~ 8.5 Hz (~7 frames/period)


def _start_timer_disabled(ics, preset: int = TIMER_PRESET) -> None:
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x42, TIMER_SCALE, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x40, preset, width=WIDTH_LOWER8)


def _stop_timer(ics) -> None:
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x40, 0x00, width=WIDTH_LOWER8)
    ics.read_reg(0, 0x40, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x42, 0x00, width=WIDTH_LOWER8)


def _wait_pending(ics, max_polls: int = 20) -> bool:
    """Poll 0x43 until timer0 pending; each poll costs >= 1 frame."""
    for _ in range(max_polls):
        if ics.read_reg(0, 0x43, width=WIDTH_LOWER8) & 0x01:
            return True
    return False


def _pending_while_disabled(ics, emit: Emit) -> None:
    _start_timer_disabled(ics)
    f0, _ = ics.get_irq_counts_timed(reset=True)
    pended = _wait_pending(ics)
    stat43 = ics.read_reg(0, 0x43, width=WIDTH_LOWER8)
    read4a = ics.read_reg(0, 0x4A, width=WIDTH_LOWER8)
    f1, counts = ics.get_irq_counts_timed()
    emit(
        "t_irq.pending_while_disabled",
        {},
        {
            "pended": pended,                  # does pending latch with enable=0?
            "stat43": stat43,
            "read4a_while_pending": read4a,    # pending vs enable readback (4A-B)
            "z80_timer0_irqs": counts.timer0,  # IRQ line must be gated: expect 0
            "z80_spurious": counts.spurious,
        },
    )

    # Latched-pending delivery: arm both enable models (hardware = 0x43 bit3,
    # RTL = 0x4A bit0); does the already-pending IRQ fire?
    ics.write_reg(0, 0x4A, 0x01, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x43, 0x08, width=WIDTH_LOWER8)
    f2, counts2 = ics.get_irq_counts_timed()
    delivered = counts2.timer0 > counts.timer0
    emit(
        "t_irq.latched_delivery",
        {},
        {"delivered_after_enable": delivered,
         "stat43_after": ics.read_reg(0, 0x43, width=WIDTH_LOWER8) & 0x01},
    )
    ics.write_reg(0, 0x43, 0x00, width=WIDTH_LOWER8)
    _stop_timer(ics)
    ics.reset_irq_counts()


def _ack_widths(ics, emit: Emit) -> None:
    # Which preset-register read widths clear the pending bit?  RTL clears on
    # either data byte; hardware decides (P2-B / 40-B).
    cases = [("lo8", WIDTH_LOWER8), ("hi8", WIDTH_UPPER8), ("w16", WIDTH_16)]
    _start_timer_disabled(ics, preset=SLOW_PRESET)
    for name, width in cases:
        pended = _wait_pending(ics)
        ics.read_reg(0, 0x40, width=width)
        stat_after = ics.read_reg(0, 0x43, width=WIDTH_LOWER8)
        emit(
            "t_irq.ack_width",
            {"width": name},
            {"pended": pended, "stat43_after_read40": stat_after & 0x01},
        )
    _stop_timer(ics)


def _stat43_read_effect(ics, emit: Emit) -> None:
    # Does reading 0x43 itself ack anything? (43-C)
    _start_timer_disabled(ics, preset=SLOW_PRESET)
    pended = _wait_pending(ics)
    first = ics.read_reg(0, 0x43, width=WIDTH_LOWER8)
    second = ics.read_reg(0, 0x43, width=WIDTH_LOWER8)
    emit(
        "t_irq.stat43_sticky",
        {},
        {"pended": pended, "first": first & 0x03, "second": second & 0x03},
    )
    _stop_timer(ics)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    _pending_while_disabled(ics, emit)
    _ack_widths(ics, emit)
    _stat43_read_effect(ics, emit)
    quiesce(ics)
