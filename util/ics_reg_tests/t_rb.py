"""T-RB: register readback, voice-select isolation, byte-width disturbance.

Matrix rows: 00-A, 01-A, 02-A/B, 06-B, 07-A/B, 09-B, 0A-A/B, 0C-B, 0D-A/B,
0E-C, 10-D, 11-A, 12-A/C, 40-C, 42-B, 43-D, 4A-B, 4B-B, 4C-A/B, 4D-A, 4F-A/B,
P1-A, P2-A.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8

from .harness import (
    NUM_VOICES,
    VOICE_REG_BASELINE,
    WIDTH_NAMES,
    Emit,
    quiesce,
    restore_voice_reg,
)

# Patterns per width.  osc_conf 0xFF sets IRQ pending+enable: the Z80 IRQ
# handler will drain it before our readback on both targets — that is real,
# comparable behavior, so we keep the pattern.
PATTERNS_8 = [0x00, 0xFF, 0xAA, 0x55]
PATTERNS_16 = [0x0000, 0xFFFF, 0xAA55, 0x5AA5]

TEST_VOICE = 2  # arbitrary non-zero voice for single-voice tests


def _voice_readback(ics, emit: Emit) -> None:
    for reg, width, _baseline in VOICE_REG_BASELINE:
        patterns = PATTERNS_16 if width == WIDTH_16 else PATTERNS_8
        for value in patterns:
            ics.write_reg(TEST_VOICE, reg, value, width=width)
            read = ics.read_reg(TEST_VOICE, reg, width=width)
            # Also capture the full 16-bit view so the unwritten byte's
            # behavior is recorded for 8-bit registers.
            read16 = ics.read_reg(TEST_VOICE, reg, width=WIDTH_16)
            emit(
                "t_rb.voice_readback",
                {"voice": TEST_VOICE, "reg": reg, "width": WIDTH_NAMES[width], "wrote": value},
                {"read": read, "read16": read16},
            )
        restore_voice_reg(ics, TEST_VOICE, reg)


def _select_isolation(ics, emit: Emit) -> None:
    # Distinct pan per voice, then read all back: proves 0x4F routing and
    # that per-voice storage does not alias between voices.
    for voice in range(NUM_VOICES):
        ics.write_reg(voice, 0x0C, (voice * 8 + 1) & 0xFF, width=WIDTH_UPPER8)
    reads = [ics.read_reg(voice, 0x0C, width=WIDTH_UPPER8) for voice in range(NUM_VOICES)]
    emit("t_rb.select_isolation", {"reg": 0x0C}, {"reads": reads})
    for voice in range(NUM_VOICES):
        restore_voice_reg(ics, voice, 0x0C)

    # VMode scope (matrix 12-A): per-voice or global?
    ics.write_reg(0, 0x12, 0x5A, width=WIDTH_UPPER8)
    other = ics.read_reg(TEST_VOICE, 0x12, width=WIDTH_UPPER8)
    same = ics.read_reg(0, 0x12, width=WIDTH_UPPER8)
    emit("t_rb.vmode_scope", {"wrote_voice": 0, "wrote": 0x5A},
         {"read_voice0": same, "read_other_voice": other})
    restore_voice_reg(ics, 0, 0x12)


def _width_disturb(ics, emit: Emit) -> None:
    # Does a single-byte write disturb the other byte of a 16-bit register?
    cases = [
        # (reg, setup16, byte_width, byte_value)
        (0x01, 0x1234, WIDTH_LOWER8, 0x56),  # OscFC low-byte write
        (0x01, 0x1234, WIDTH_UPPER8, 0x78),  # OscFC high-byte write
        (0x09, 0x4321, WIDTH_LOWER8, 0x9A),  # VolAcc low-byte write
        (0x09, 0x4321, WIDTH_UPPER8, 0xBC),  # VolAcc high-byte write
        (0x0A, 0x2468, WIDTH_LOWER8, 0xDE),  # OscAcc hi reg, low-byte write
        (0x0A, 0x2468, WIDTH_UPPER8, 0xF0),  # OscAcc hi reg, high-byte write
    ]
    for reg, setup, width, byte_value in cases:
        ics.write_reg(TEST_VOICE, reg, setup, width=WIDTH_16)
        ics.write_reg(TEST_VOICE, reg, byte_value, width=width)
        read16 = ics.read_reg(TEST_VOICE, reg, width=WIDTH_16)
        emit(
            "t_rb.width_disturb",
            {"voice": TEST_VOICE, "reg": reg, "setup": setup,
             "byte_width": WIDTH_NAMES[width], "byte_value": byte_value},
            {"read16": read16},
        )
        restore_voice_reg(ics, TEST_VOICE, reg)


def _globals(ics, emit: Emit) -> None:
    # 0x0E ActiveOsc readback (restore 0x1F).
    for value in (0x1F, 0x15, 0xFF):
        ics.write_reg(0, 0x0E, value, width=WIDTH_UPPER8)
        read = ics.read_reg(0, 0x0E, width=WIDTH_UPPER8)
        read16 = ics.read_reg(0, 0x0E, width=WIDTH_16)
        emit("t_rb.active_osc", {"wrote": value}, {"read": read, "read16": read16})
    ics.write_reg(0, 0x0E, 0x1F, width=WIDTH_UPPER8)

    # IRQV idle value (quiesced: nothing pending).
    emit("t_rb.irqv_idle", {},
         {"hi8": ics.read_reg(0, 0x0F, width=WIDTH_UPPER8),
          "w16": ics.read_reg(0, 0x0F, width=WIDTH_16)})

    # Timer presets: write/readback with IRQs disabled, timers re-stopped after.
    for reg in (0x40, 0x41):
        for value in (0x00, 0x7F, 0xFF):
            ics.write_reg(0, reg, value, width=WIDTH_LOWER8)
            read = ics.read_reg(0, reg, width=WIDTH_LOWER8)
            emit("t_rb.timer_preset", {"reg": reg, "wrote": value}, {"read": read})
        ics.write_reg(0, reg, 0x00, width=WIDTH_LOWER8)
        ics.read_reg(0, reg, width=WIDTH_LOWER8)  # ack any IRQ this provoked

    # Timer scales: 0x42 read is undecoded in the RTL (matrix 42-B); 0x43
    # read is IRQ status while its write is timer-1 scale (43-B/43-D).
    for reg in (0x42, 0x43):
        for value in (0x00, 0x5A, 0xFF):
            ics.write_reg(0, reg, value, width=WIDTH_LOWER8)
            read = ics.read_reg(0, reg, width=WIDTH_LOWER8)
            emit("t_rb.timer_scale", {"reg": reg, "wrote": value}, {"read": read})
        ics.write_reg(0, reg, 0x00, width=WIDTH_LOWER8)

    # 0x4A: write enable mask, read back (RTL/MAME return pending instead).
    for value in (0x00, 0x03, 0xFF):
        ics.write_reg(0, 0x4A, value, width=WIDTH_LOWER8)
        read = ics.read_reg(0, 0x4A, width=WIDTH_LOWER8)
        emit("t_rb.irq_enable", {"wrote": value}, {"read": read})
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)

    # 0x4B idle reads (matrix 4B-B): nothing pending after quiesce.
    emit("t_rb.osc_vector_idle", {},
         {"reads": [ics.read_reg(0, 0x4B, width=WIDTH_LOWER8) for _ in range(3)]})

    # 0x4C: revision read, write sensitivity (BIOS writes 3 during init).
    initial = ics.read_reg(0, 0x4C, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4C, 0x03, width=WIDTH_LOWER8)
    after3 = ics.read_reg(0, 0x4C, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4C, 0xA5, width=WIDTH_LOWER8)
    aftera5 = ics.read_reg(0, 0x4C, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4C, 0x03, width=WIDTH_LOWER8)  # BIOS-like state
    emit("t_rb.revision", {}, {"initial": initial, "after_w3": after3, "after_wa5": aftera5})

    # 0x4D walking-bit readback (matrix 4D-A, the highest-value unknown).
    initial = ics.read_reg(0, 0x4D, width=WIDTH_LOWER8)
    reads = {}
    for value in (0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF, 0x0C):
        ics.write_reg(0, 0x4D, value, width=WIDTH_LOWER8)
        reads[f"{value:#04x}"] = ics.read_reg(0, 0x4D, width=WIDTH_LOWER8)
    # Restore the driver boot state 0x0D, NOT the BIOS-decompile 0x0C: bit 0
    # gates timer counting on hardware and the rest of the suite needs timers.
    ics.write_reg(0, 0x4D, 0x0D, width=WIDTH_LOWER8)
    emit("t_rb.system_control", {}, {"initial": initial, "reads": reads})

    # 0x4F: is osc_select readable? (matrix 4F-B)
    reads = {}
    for value in (0x00, 0x07, 0x1F):
        ics.write_reg(0, 0x4F, value, width=WIDTH_LOWER8)
        reads[f"{value:#04x}"] = ics.read_reg(0, 0x4F, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4F, 0x00, width=WIDTH_LOWER8)
    emit("t_rb.osc_select_read", {}, {"reads": reads})


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    _voice_readback(ics, emit)
    _select_isolation(ics, emit)
    _width_disturb(ics, emit)
    _globals(ics, emit)
