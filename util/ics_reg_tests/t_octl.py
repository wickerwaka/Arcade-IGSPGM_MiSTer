"""T-OCTL: OscCtl (0x10) semantics beyond the audio-effect check in T-AUD.

Matrix rows: 10-C (reprogram gate), 10-D (readback shape), 10-E (MAME's
timer-start-bit guess).

Register/IRQ-level only (no audio): the audible key-on variants are in T-AUD.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8, Voice

from .harness import Emit, quiesce

V = 6


def _readback(ics, emit: Emit) -> None:
    # OscCtl readback: which bits are stored, what do unwritten bits read?
    reads = {}
    for value in (0x00, 0x01, 0x02, 0x0F, 0x10, 0x20, 0x30, 0xFF, 0xAA, 0x55):
        ics.write_reg(V, 0x10, value, width=WIDTH_UPPER8)
        reads[f"{value:#04x}"] = ics.read_reg(V, 0x10, width=WIDTH_UPPER8)
    ics.write_reg(V, 0x10, 0x0F, width=WIDTH_UPPER8)
    emit("t_octl.readback", {"voice": V}, {"reads": reads})


def _timer_start_guess(ics, emit: Emit) -> None:
    # MAME (ics2115.cpp:843) guesses OscCtl bits 0/1 start timers 1/2.  We now
    # know timer IRQ output is 0x43-gated and counting is 0x4D-gated; this
    # records whether an OscCtl write moves timer counts with the 0x43 enables
    # OFF — if so MAME's guess has merit, if not it's debunked.
    ics.write_reg(0, 0x42, 0xFF, width=WIDTH_LOWER8)  # timer0 scale ~103Hz
    ics.write_reg(0, 0x40, 0x04, width=WIDTH_LOWER8)  # preset (counting via 0x4D bit0)
    ics.write_reg(0, 0x43, 0x00, width=WIDTH_LOWER8)  # IRQ output disabled
    for ctl in (0x01, 0x02, 0x03):
        ics.write_reg(V, 0x10, ctl, width=WIDTH_UPPER8)
        f0, _ = ics.get_irq_counts_timed(reset=True)
        while True:
            f1, c = ics.get_irq_counts_timed()
            if f1 - f0 >= 10:
                break
        emit("t_octl.timer_start_guess", {"octl": ctl},
             {"timer0_irqs": c.timer0, "timer1_irqs": c.timer1, "spurious": c.spurious})
        ics.write_reg(V, 0x10, 0x0F, width=WIDTH_UPPER8)
    ics.write_reg(0, 0x40, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x42, 0x00, width=WIDTH_LOWER8)


def _reprogram_gate(ics, emit: Emit) -> None:
    # Turtle Beach writes OscCtl shadow|0x02 transiently while reprogramming a
    # live voice (UpdateModulatedVoiceParameter).  Record the voice runtime
    # state across the gate: does bit1 stop and a 0x00 re-key resume cleanly?
    v = Voice.from_bios_trace()
    v.osc_conf = 0x08  # loop, keeps running
    v.osc_ctl = 0x00
    ics.write_voice(V, v)
    running = ics.read_reg(V, 0x10, width=WIDTH_UPPER8)
    ics.write_reg(V, 0x10, 0x02, width=WIDTH_UPPER8)   # shadow|0x02 gate
    gated = ics.read_reg(V, 0x10, width=WIDTH_UPPER8)
    osc_acc_gated = ics.read_reg(V, 0x0A, width=WIDTH_16)
    ics.write_reg(V, 0x10, 0x00, width=WIDTH_UPPER8)   # re-key
    resumed = ics.read_reg(V, 0x10, width=WIDTH_UPPER8)
    emit("t_octl.reprogram_gate", {"voice": V},
         {"running": running, "gated": gated, "osc_acc_gated": osc_acc_gated, "resumed": resumed})
    ics.write_reg(V, 0x10, 0x0F, width=WIDTH_UPPER8)
    ics.write_reg(V, 0x00, 0x00, width=WIDTH_UPPER8)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    _readback(ics, emit)
    _timer_start_guess(ics, emit)
    _reprogram_gate(ics, emit)
    quiesce(ics)
