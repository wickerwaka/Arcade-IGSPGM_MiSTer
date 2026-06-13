"""T-SYS: system/global register semantics not covered elsewhere.

Matrix rows: P0-A/B/C (port-0 status), 4B-A (vector on voice != 0), 4B-C
(bits 6:5), 4C-B (write effect), 4D-B/C (system-control bits beyond timer
gating).  Register/IRQ-level; no audio.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8

from .harness import NUM_VOICES, Emit, quiesce


def _port0_status(ics, emit: Emit) -> None:
    # Decode the port-0 status byte across IRQ states.
    states = {}
    # (a) quiesced idle
    states["idle"] = ics.read_status_port()
    # (b) timer0 pending + enabled
    ics.write_reg(0, 0x42, 0xFF, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x40, 0x04, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x43, 0x08, width=WIDTH_LOWER8)
    f0, _ = ics.get_irq_counts_timed(reset=True)
    while True:
        f1, _ = ics.get_irq_counts_timed()
        if f1 - f0 >= 3:
            break
    states["timer_active"] = ics.read_status_port()
    ics.write_reg(0, 0x43, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x40, 0x00, width=WIDTH_LOWER8)
    # (c) voice osc IRQ pending (host pend, 0x4A enabled)
    ics.write_reg(0, 0x4A, 0x01, width=WIDTH_LOWER8)
    ics.write_reg(3, 0x00, 0xA0, width=WIDTH_UPPER8)
    states["voice_pend"] = ics.read_status_port()
    ics.write_reg(3, 0x00, 0x00, width=WIDTH_UPPER8)
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)
    ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)
    emit("t_sys.port0_status", {}, states)


def _vector_per_voice(ics, emit: Emit) -> None:
    # 4B-A: does 0x4B report 0x80|voice for voices other than 0?  Pend each
    # voice's osc IRQ in isolation and read the vector.
    reads = {}
    for voice in (0, 2, 7, 17, 31):
        ics.write_reg(voice, 0x00, 0xA0, width=WIDTH_UPPER8)
        reads[str(voice)] = ics.read_reg(0, 0x4B, width=WIDTH_LOWER8)
        ics.write_reg(voice, 0x00, 0x00, width=WIDTH_UPPER8)
        ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)  # consume
    emit("t_sys.vector_per_voice", {}, {"reads": reads})


def _revision_write(ics, emit: Emit) -> None:
    # 4C-B: does writing 0x4C change anything observable?
    before = ics.read_reg(0, 0x4C, width=WIDTH_LOWER8)
    snaps = {}
    for value in (0x00, 0x03, 0xFF):
        ics.write_reg(0, 0x4C, value, width=WIDTH_LOWER8)
        snaps[f"{value:#04x}"] = ics.read_reg(0, 0x4C, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4C, 0x03, width=WIDTH_LOWER8)
    emit("t_sys.revision_write", {}, {"before": before, "after": snaps})


def _sys_ctl_bits(ics, emit: Emit) -> None:
    # 4D-B/C: per-bit effect on timer counting (bit0 known gate) and any
    # cross-effect on a configured timer.  For each writable bit, set it (plus
    # the run bit) and measure timer0 counts.
    results = {}
    for bit in (0x00, 0x01, 0x04, 0x05, 0x0C, 0x0D):
        ics.write_reg(0, 0x4D, bit, width=WIDTH_LOWER8)
        readback = ics.read_reg(0, 0x4D, width=WIDTH_LOWER8)
        ics.write_reg(0, 0x42, 0xFF, width=WIDTH_LOWER8)
        ics.write_reg(0, 0x40, 0x04, width=WIDTH_LOWER8)
        ics.write_reg(0, 0x43, 0x08, width=WIDTH_LOWER8)
        f0, _ = ics.get_irq_counts_timed(reset=True)
        while True:
            f1, c = ics.get_irq_counts_timed()
            if f1 - f0 >= 8:
                break
        ics.write_reg(0, 0x43, 0x00, width=WIDTH_LOWER8)
        ics.write_reg(0, 0x40, 0x00, width=WIDTH_LOWER8)
        results[f"{bit:#04x}"] = {"readback": readback, "counting": c.timer0 > 0}
    ics.write_reg(0, 0x4D, 0x0D, width=WIDTH_LOWER8)
    emit("t_sys.sys_ctl_bits", {}, results)


def _sys_ctl_isolation(ics, emit: Emit) -> None:
    # Separate bit0 vs bit2: for each 0x4D state, record whether (a) the timer
    # counts and (b) a voice osc-IRQ is delivered to the Z80.  If the two
    # subsystems gate differently, bit0/bit2 have distinct roles; if they gate
    # identically, the pair is a single combined enable.
    for sysd in (0x00, 0x01, 0x04, 0x05):
        ics.write_reg(0, 0x4D, sysd, width=WIDTH_LOWER8)
        # (a) timer counting
        ics.write_reg(0, 0x42, 0xFF, width=WIDTH_LOWER8)
        ics.write_reg(0, 0x40, 0x04, width=WIDTH_LOWER8)
        ics.write_reg(0, 0x43, 0x08, width=WIDTH_LOWER8)
        f0, _ = ics.get_irq_counts_timed(reset=True)
        while True:
            f1, ct = ics.get_irq_counts_timed()
            if f1 - f0 >= 8:
                break
        ics.write_reg(0, 0x43, 0x00, width=WIDTH_LOWER8)
        ics.write_reg(0, 0x40, 0x00, width=WIDTH_LOWER8)
        # (b) voice osc-IRQ delivery (0x4A gate on, host pend voice 3)
        ics.write_reg(0, 0x4A, 0x01, width=WIDTH_LOWER8)
        ics.reset_irq_counts()
        ics.write_reg(3, 0x00, 0xA0, width=WIDTH_UPPER8)
        f0, _ = ics.get_irq_counts_timed()
        while True:
            f1, cv = ics.get_irq_counts_timed()
            if f1 - f0 >= 3:
                break
        ics.write_reg(3, 0x00, 0x00, width=WIDTH_UPPER8)
        ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)
        ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)
        emit("t_sys.sys_ctl_isolation", {"sys_ctl": sysd},
             {"timer_counting": ct.timer0 > 0, "voice_irq_delivered": cv.osc > 0})
    ics.write_reg(0, 0x4D, 0x0D, width=WIDTH_LOWER8)


def _reset_strobe(ics, emit: Emit) -> None:
    # Does the 0x4D bit0 low->high strobe (BIOS ResetSoundChipMixerState) reset
    # registers to known values, or merely gate run state?  Stamp distinctive
    # values, strobe, read back: snapped-to-default => state-clearing reset;
    # retained => bit0 is only a run gate.
    ics.write_reg(5, 0x0C, 0xA0, width=WIDTH_UPPER8)   # voice 5 pan
    ics.write_reg(5, 0x01, 0x1234, width=WIDTH_16)      # voice 5 fc
    ics.write_reg(0, 0x42, 0x5A, width=WIDTH_LOWER8)    # timer0 scale
    ics.write_reg(0, 0x40, 0x33, width=WIDTH_LOWER8)    # timer0 preset
    ics.write_reg(7, 0x00, 0xA0, width=WIDTH_UPPER8)    # voice 7 osc pend
    # Register fields only — IRQV is excluded (its pointer/pend state is noisy
    # across the strobe and not a "did registers reset" signal).
    def snap():
        return {
            "pan5": ics.read_reg(5, 0x0C, width=WIDTH_UPPER8),
            "fc5": ics.read_reg(5, 0x01, width=WIDTH_16),
            "preset0": ics.read_reg(0, 0x40, width=WIDTH_LOWER8),
        }
    before = snap()
    # Strobe bit0 low -> settle -> high (BIOS shape; bit2 kept set).
    ics.write_reg(0, 0x4D, 0x04, width=WIDTH_LOWER8)
    for _ in range(16):
        ics.read_reg(0, 0x4D, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4D, 0x05, width=WIDTH_LOWER8)
    after = snap()
    emit("t_sys.reset_strobe", {}, {"before": before, "after": after,
                                    "registers_reset": before != after})
    ics.write_reg(7, 0x00, 0x00, width=WIDTH_UPPER8)
    ics.write_reg(0, 0x4D, 0x0D, width=WIDTH_LOWER8)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    _port0_status(ics, emit)
    _vector_per_voice(ics, emit)
    _sys_ctl_isolation(ics, emit)
    _reset_strobe(ics, emit)
    _revision_write(ics, emit)
    _sys_ctl_bits(ics, emit)
    quiesce(ics)
