"""T-VOICE: real oscillator-generated IRQs (vs host-forced pends).

Matrix rows: 00-A (bit7 set by hardware), 00-C/D follow-ups, 0D-C, P0-A.

Host-forced pends never assert the IRQ output on hardware; these tests use a
real one-shot voice (the BIOS-trace sample, ~0.7 s to its end address) so the
oscillator itself generates the end event.  The key question for the RTL IRQ
model: does a real osc-end assert the line, and through which enables?
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_LOWER8, WIDTH_UPPER8, Voice

from .harness import Emit, quiesce

TEST_VOICE = 4
WAIT_VBLANKS = 90  # sample runs ~0.7 s = ~42 vblanks; generous margin


def _wait_for(ics, predicate, vblanks: int):
    f0, counts = ics.get_irq_counts_timed()
    while True:
        f1, counts = ics.get_irq_counts_timed()
        if predicate(counts) or f1 - f0 >= vblanks:
            return f1 - f0, counts


def _play(ics, conf: int) -> None:
    voice = Voice.from_bios_trace()
    voice.osc_conf = conf
    voice.osc_ctl = 0x00  # key on
    ics.write_voice(TEST_VOICE, voice)


def _stop(ics) -> None:
    ics.write_reg(TEST_VOICE, 0x10, 0x0F, width=WIDTH_UPPER8)
    ics.write_reg(TEST_VOICE, 0x00, 0x00, width=WIDTH_UPPER8)
    ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)  # drain any leftover


def _oneshot_end_irq(ics, emit: Emit) -> None:
    # One-shot, osc IRQ enable (bit5) + 0x4A=1: hardware delivers the INT
    # (0x4A is the voice-IRQ output enable; the Z80 driver acks by clearing
    # conf bit7).
    ics.write_reg(0, 0x4A, 0x01, width=WIDTH_LOWER8)
    ics.reset_irq_counts()
    ics.clear_irq_log()
    _play(ics, 0x20)
    frames, counts = _wait_for(ics, lambda c: c.osc > 0, WAIT_VBLANKS)
    log = ics.get_irq_log()
    emit(
        "t_voice.oneshot_end_irq",
        {"voice": TEST_VOICE, "conf": 0x20},
        {
            "z80_osc_irqs": counts.osc,
            "z80_vol_irqs": counts.vol,
            "z80_spurious": counts.spurious,
            # First entry only: hardware can double-service before the ack
            # lands (the end condition re-latches); the vector value is what
            # must match.
            "log": [[e.kind, e.a, e.b] for e in log[:1]],
            "conf_after": ics.read_reg(TEST_VOICE, 0x00, width=WIDTH_UPPER8),
            "vec_4b_after": ics.read_reg(0, 0x4B, width=WIDTH_LOWER8),
            "irqv_after": ics.read_reg(0, 0x0F, width=WIDTH_UPPER8),
        },
    )
    _stop(ics)
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)

    # Same one-shot with 0x4A=0: the pend must stay silent (gating proof).
    ics.reset_irq_counts()
    _play(ics, 0x20)
    frames, counts = _wait_for(ics, lambda c: c.osc > 0, WAIT_VBLANKS)
    emit(
        "t_voice.oneshot_gated",
        {"voice": TEST_VOICE, "conf": 0x20, "irq_4a": 0},
        {"z80_osc_irqs": counts.osc,
         "conf_after": ics.read_reg(TEST_VOICE, 0x00, width=WIDTH_UPPER8)},
    )
    _stop(ics)


def _oneshot_no_enable(ics, emit: Emit) -> None:
    # One-shot WITHOUT bit5: does the end event pend silently, or nothing?
    ics.reset_irq_counts()
    _play(ics, 0x00)
    frames, counts = _wait_for(ics, lambda c: False, WAIT_VBLANKS)
    emit(
        "t_voice.oneshot_no_enable",
        {"voice": TEST_VOICE, "conf": 0x00},
        {
            "z80_osc_irqs": counts.osc,
            "conf_after": ics.read_reg(TEST_VOICE, 0x00, width=WIDTH_UPPER8),
            "vec_4b": ics.read_reg(0, 0x4B, width=WIDTH_LOWER8),
            "irqv": ics.read_reg(0, 0x0F, width=WIDTH_UPPER8),
        },
    )
    _stop(ics)


def _loop_end_irqs(ics, emit: Emit) -> None:
    # Looping voice with bit5: does every loop wrap fire an IRQ?  The
    # BIOS-trace sample loops in ~0.7 s, so expect ~2-3 over 150 vblanks.
    ics.write_reg(0, 0x4A, 0x01, width=WIDTH_LOWER8)
    ics.reset_irq_counts()
    _play(ics, 0x28)  # loop + IRQ enable
    frames, counts = _wait_for(ics, lambda c: False, 150)
    emit(
        "t_voice.loop_end_irqs",
        {"voice": TEST_VOICE, "conf": 0x28, "frames": 150},
        {"z80_osc_irqs": counts.osc,
         "conf_after": ics.read_reg(TEST_VOICE, 0x00, width=WIDTH_UPPER8)},
    )
    _stop(ics)
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)


def _vol_ramp_end_irq(ics, emit: Emit) -> None:
    # Volume ramp to VolEnd with VCtl bit5: the envelope-side IRQ source.
    ics.write_reg(0, 0x4A, 0x01, width=WIDTH_LOWER8)
    ics.reset_irq_counts()
    ics.clear_irq_log()
    voice = Voice.from_bios_trace()
    voice.osc_conf = 0x08      # loop, no osc IRQ — keep the voice running
    voice.vol_acc = 0x0000
    voice.vol_start = 0x00
    voice.vol_end = 0xFF
    voice.vol_incr = 0x10
    voice.vol_ctrl = 0x20      # ramp up, vol IRQ enable
    voice.osc_ctl = 0x00
    ics.write_voice(TEST_VOICE, voice)
    frames, counts = _wait_for(ics, lambda c: c.vol > 0, WAIT_VBLANKS)
    log = ics.get_irq_log()
    emit(
        "t_voice.vol_ramp_end_irq",
        {"voice": TEST_VOICE, "vol_incr": 0x10, "vol_ctrl": 0x20},
        {
            "z80_vol_irqs": counts.vol,
            "z80_osc_irqs": counts.osc,
            "log": [[e.kind, e.a, e.b] for e in log],
            "vctl_after": ics.read_reg(TEST_VOICE, 0x0D, width=WIDTH_UPPER8),
            "irqv_after": ics.read_reg(0, 0x0F, width=WIDTH_UPPER8),
        },
    )
    ics.write_reg(TEST_VOICE, 0x0D, 0x03, width=WIDTH_UPPER8)
    _stop(ics)
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    _oneshot_end_irq(ics, emit)
    _oneshot_no_enable(ics, emit)
    _loop_end_irqs(ics, emit)
    _vol_ramp_end_irq(ics, emit)
    quiesce(ics)
