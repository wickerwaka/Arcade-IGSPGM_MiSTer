"""Shared helpers for the ICS2115 register conformance tests.

Conventions that keep records diffable across targets:
- record only register values and deltas, never timing or wall-clock data
- quiesce() puts the chip in a fixed baseline before every group
- every test that perturbs a register restores its baseline value afterwards

Width/value convention (testroms/z80_ics_driver.c): 8-bit accesses carry the
byte in the LOW 8 bits of the command value/result, regardless of which data
port (hi/lo) they touch.
"""

from __future__ import annotations

from typing import Any, Callable

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8, Voice

Emit = Callable[[str, dict[str, Any], dict[str, Any]], None]

WIDTH_NAMES = {WIDTH_16: "w16", WIDTH_UPPER8: "hi8", WIDTH_LOWER8: "lo8"}

NUM_VOICES = 32

# Per-voice registers with their natural access width and a safe baseline
# value.  Baseline keeps voices keyed off with fc=0 and vol_incr=0 so even a
# "running" state cannot move between a write and the readback one command
# later.
VOICE_REG_BASELINE: list[tuple[int, int, int]] = [
    # (reg, width, baseline)
    (0x00, WIDTH_UPPER8, 0x00),  # OscConf
    (0x01, WIDTH_16, 0x0000),    # OscFC
    (0x02, WIDTH_16, 0x0000),    # OscStart hi
    (0x03, WIDTH_UPPER8, 0x00),  # OscStart lo
    (0x04, WIDTH_16, 0x0000),    # OscEnd hi
    (0x05, WIDTH_UPPER8, 0x00),  # OscEnd lo
    (0x06, WIDTH_UPPER8, 0x00),  # VolIncr
    (0x07, WIDTH_UPPER8, 0x00),  # VolStart
    (0x08, WIDTH_UPPER8, 0x00),  # VolEnd
    (0x09, WIDTH_16, 0x0000),    # VolAcc
    (0x0A, WIDTH_16, 0x0000),    # OscAcc hi
    (0x0B, WIDTH_16, 0x0000),    # OscAcc lo
    (0x0C, WIDTH_UPPER8, 0x00),  # Pan
    (0x0D, WIDTH_UPPER8, 0x03),  # VCtl: done+stop (BIOS idle state)
    (0x10, WIDTH_UPPER8, 0x0F),  # OscCtl: keyed off
    (0x11, WIDTH_UPPER8, 0x00),  # OscSAddr
    (0x12, WIDTH_UPPER8, 0x00),  # VMode
]

BASELINE_VOICE = Voice(vol_ctrl=0x03, osc_ctl=0x0F)

# Globals snapshotted for alias detection.  Read-only here: 0x4D write
# behavior is its own test and unknown on hardware.
GLOBAL_SNAPSHOT_REGS = [0x0E, 0x40, 0x41, 0x42, 0x43, 0x4A, 0x4B, 0x4C, 0x4D, 0x4F]


def quiesce(ics) -> None:
    """Put the chip into the fixed test baseline."""
    # Chip run state: 0x4D bit0 gates timer counting on hardware (stored
    # mask 0x05; the driver boot state is 0x0D).  RTL ignores this write.
    ics.write_reg(0, 0x4D, 0x0D, width=WIDTH_LOWER8)
    # Disable timer IRQs (both models: 0x43 control bits on hardware, 0x4A
    # mask in the current RTL), stop timers.
    ics.write_reg(0, 0x43, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x40, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x41, 0x00, width=WIDTH_LOWER8)
    # Ack any pending timer IRQs (read has the clear side effect).
    ics.read_reg(0, 0x40, width=WIDTH_LOWER8)
    ics.read_reg(0, 0x41, width=WIDTH_LOWER8)
    # All voices visible, all voices to baseline (bulk write per voice).
    ics.write_reg(0, 0x0E, 0x1F, width=WIDTH_UPPER8)
    for voice in range(NUM_VOICES):
        ics.write_voice(voice, BASELINE_VOICE)
    # Drain any pending voice IRQs (IRQV idle reads as bits 7:6 set).
    for _ in range(2 * NUM_VOICES):
        irqv = ics.read_reg(0, 0x0F, width=WIDTH_UPPER8)
        if (irqv & 0xC0) == 0xC0:
            break
    # Clear the Z80-side IRQ counters so later deltas start from zero.
    ics.reset_irq_counts()


def restore_voice_reg(ics, voice: int, reg: int) -> None:
    for r, width, baseline in VOICE_REG_BASELINE:
        if r == reg:
            ics.write_reg(voice, r, baseline, width=width)
            return


def snapshot_voice(ics, voice: int) -> str:
    """All 24 raw voice bytes as hex (one bulk command)."""
    return ics.read_voice(voice).pack().hex()


def snapshot_globals(ics) -> dict[str, int]:
    # Width w16 everywhere: stable shape, exercises both data ports.
    return {f"{reg:#04x}": ics.read_reg(0, reg, width=WIDTH_16)
            for reg in GLOBAL_SNAPSHOT_REGS}


def dict_delta(before: dict[str, int], after: dict[str, int]) -> dict[str, list[int]]:
    return {key: [before[key], after[key]] for key in before if before[key] != after[key]}
