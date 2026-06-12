"""T-TMR: timer period measurement against the MAME-derived formula.

Matrix rows: 40-A, 40-D, 42-A, 43-B.

period_clocks = ((scale & 0x1f) + 1) * (preset + 1) << (4 + (scale >> 5))

IRQs are counted by the Z80 against the TestROM's vblank frame counter
(I-1), so the measurement is target-independent.  Counts get a small
tolerance in the differ (timer phase vs frame boundary).

Grid constraints (both measured in sim, 2026-06-11):
- periods below ~17000 chip clocks (~2 kHz) IRQ-storm the Z80 and starve the
  command loop entirely;
- the Z80 IRQ service loop saturates at ~830 Hz: above that, fires merge into
  the still-pending bit and counts read exactly half.  The ceiling differs
  between sim and hardware (ICS port wait states), so all grid points stay
  <= ~420 Hz where counts are exact on both targets.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_LOWER8

from .harness import Emit, quiesce

# (timer, scale, preset, frames) — all rates <= ~420 Hz (see ceiling note).
GRID = [
    # multiplier field validation (shift fixed at 7)
    (0, 0xE0, 255, 30),   # mult 1:  524288 clk ~  64.6 Hz
    (0, 0xE7, 255, 40),   # mult 8: 4194304 clk ~   8.1 Hz
    (0, 0xFF, 4, 20),     # mult 32: 327680 clk ~ 103.4 Hz
    (0, 0xFF, 255, 60),   # slowest: 16.7M clk  ~   2.0 Hz
    # shift field validation (mult 32, preset 255)
    (0, 0x1F, 255, 20),   # shift 4: 131072 clk ~ 258.4 Hz
    (0, 0x3F, 255, 20),   # shift 5: 262144 clk ~ 129.2 Hz
    (0, 0x44, 255, 20),   # shift 6 (mult 5): 81920 clk ~ 413.4 Hz
    # timer 1: hardware model is shift-only from 0x43[7:5], no multiplier
    # (period1 = (preset+1) << (4+shift)); the "scale" field below is the
    # 0x43 control byte minus the enable bit.  RTL rates will diverge until
    # it adopts the hardware model — that divergence is the recorded finding.
    (1, 0xE0, 255, 30),   # shift 7: 524288 clk ~  64.6 Hz
    (1, 0xA0, 255, 20),   # shift 5: 131072 clk ~ 258.4 Hz
]

# preset=0: RTL stops the timer; the formula's (0+1) would give 65536 clk
# (~516 Hz).  Hardware decides (matrix 40-D).
PRESET_ZERO = [(0, 0xFF, 0, 15)]


def _measure(ics, emit: Emit, timer: int, scale: int, preset: int, frames: int, test_id: str) -> None:
    preset_reg = 0x40 + timer

    # Arm BOTH enable models so the measurement runs on either target until
    # the RTL adopts the hardware model: hardware = 0x43 control (bit3 = t0
    # enable, bit4 = t1 enable, t1 shift in [7:5]); RTL/MAME = 0x4A mask +
    # 0x43 as timer-1 scale.  For timer 0 the 0x43 write is 0x08 (enable
    # only); for timer 1 the scale value doubles as the control byte with
    # bit4 forced — on hardware only [7:5] and bit4 act.
    if timer == 0:
        ics.write_reg(0, 0x42, scale, width=WIDTH_LOWER8)
        control = 0x08
    else:
        control = scale | 0x10
    ics.write_reg(0, preset_reg, preset, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4A, 1 << timer, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x43, control, width=WIDTH_LOWER8)

    f0, _ = ics.get_irq_counts_timed(reset=True)
    while True:
        f1, counts = ics.get_irq_counts_timed()
        if f1 - f0 >= frames:
            break

    # Stop and ack.
    ics.write_reg(0, 0x43, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, 0x4A, 0x00, width=WIDTH_LOWER8)
    ics.write_reg(0, preset_reg, 0x00, width=WIDTH_LOWER8)
    ics.read_reg(0, preset_reg, width=WIDTH_LOWER8)
    if timer == 0:
        ics.write_reg(0, 0x42, 0x00, width=WIDTH_LOWER8)

    count = counts.timer0 if timer == 0 else counts.timer1
    other = counts.timer1 if timer == 0 else counts.timer0
    emit(
        test_id,
        {"timer": timer, "scale": scale, "preset": preset, "frames_min": frames},
        {"frames": f1 - f0, "count": count, "other_timer_count": other,
         "spurious": counts.spurious},
    )


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    for timer, scale, preset, frames in GRID:
        _measure(ics, emit, timer, scale, preset, frames, "t_tmr.rate")
    for timer, scale, preset, frames in PRESET_ZERO:
        _measure(ics, emit, timer, scale, preset, frames, "t_tmr.preset_zero")
    quiesce(ics)
